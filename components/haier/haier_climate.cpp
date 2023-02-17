#include <chrono>
#include <string>
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "haier_climate.h"
#include "haier_packet.h"

using namespace esphome::climate;
using namespace esphome::uart;

#ifndef ESPHOME_LOG_LEVEL
#warning "No ESPHOME_LOG_LEVEL defined!"
#endif

namespace esphome {
namespace haier {

const char TAG[] = "haier.climate";
constexpr size_t COMMUNICATION_TIMEOUT_MS =         60000;
constexpr size_t STATUS_REQUEST_INTERVAL_MS =       5000;
constexpr size_t DEFAULT_MESSAGES_INTERVAL_MS =     2000;
constexpr size_t CONTROL_MESSAGES_INTERVAL_MS =     400;
constexpr size_t CONTROL_TIMEOUT_MS =               7000;

HaierClimate::HaierClimate(UARTComponent *parent)
    : UARTDevice(parent),
      haier_protocol_(*this),
      protocol_phase_(ProtocolPhases::SENDING_FIRST_STATUS_REQUEST),
      last_status_message_(new uint8_t[sizeof(smartair2_protocol::HaierPacketControl)]),
      fan_mode_speed_((uint8_t) smartair2_protocol::FanMode::FAN_MID),
      other_modes_fan_speed_((uint8_t) smartair2_protocol::FanMode::FAN_AUTO),
      display_status_(true),
      force_send_control_(false),
      forced_publish_(false),
      forced_request_status_(false),
      control_called_(false) {  
  this->traits_ = climate::ClimateTraits();
  this->traits_.set_supported_modes({
    climate::CLIMATE_MODE_OFF,
    climate::CLIMATE_MODE_COOL,
    climate::CLIMATE_MODE_HEAT,
    climate::CLIMATE_MODE_FAN_ONLY,
    climate::CLIMATE_MODE_DRY,
    climate::CLIMATE_MODE_AUTO
  });
  this->traits_.set_supported_fan_modes({
    climate::CLIMATE_FAN_AUTO,
    climate::CLIMATE_FAN_LOW,
    climate::CLIMATE_FAN_MEDIUM,
    climate::CLIMATE_FAN_HIGH,
  });
  this->traits_.set_supported_swing_modes({
    climate::CLIMATE_SWING_OFF,
    climate::CLIMATE_SWING_BOTH,
    climate::CLIMATE_SWING_VERTICAL,
    climate::CLIMATE_SWING_HORIZONTAL
  });
  this->traits_.set_supports_current_temperature(true);
}

HaierClimate::~HaierClimate() {}

void HaierClimate::set_phase_(ProtocolPhases phase) {
  if (this->protocol_phase_ != phase) {
    ESP_LOGV(TAG, "Phase transition: %d => %d", this->protocol_phase_, phase);
    this->protocol_phase_ = phase;
  }
}

bool HaierClimate::get_display_state() const { return this->display_status_; }

void HaierClimate::set_display_state(bool state) {
  if (this->display_status_ != state) {
    this->display_status_ = state;
    this->force_send_control_ = true;
  }
}

void HaierClimate::set_supported_swing_modes(const std::set<climate::ClimateSwingMode> &modes) {
  this->traits_.set_supported_swing_modes(modes);
  this->traits_.add_supported_swing_mode(climate::CLIMATE_SWING_OFF);       // Always available
  this->traits_.add_supported_swing_mode(climate::CLIMATE_SWING_VERTICAL);  // Always available
}

void HaierClimate::set_supported_modes(const std::set<climate::ClimateMode> &modes) {
  this->traits_.set_supported_modes(modes);
  this->traits_.add_supported_mode(climate::CLIMATE_MODE_OFF);   // Always available
  this->traits_.add_supported_mode(climate::CLIMATE_MODE_AUTO);  // Always available
}

haier_protocol::HandlerError HaierClimate::answer_preprocess_(uint8_t requestMessageType,
                                                              uint8_t expectedRequestMessageType,
                                                              uint8_t answerMessageType,
                                                              uint8_t expectedAnswerMessageType,
                                                              ProtocolPhases expectedPhase) {
  haier_protocol::HandlerError result = haier_protocol::HandlerError::HANDLER_OK;
  if ((expectedRequestMessageType != (uint8_t) smartair2_protocol::FrameType::NO_COMMAND) &&
      (requestMessageType != expectedRequestMessageType))
    result = haier_protocol::HandlerError::UNSUPORTED_MESSAGE;
  if ((expectedAnswerMessageType != (uint8_t) smartair2_protocol::FrameType::NO_COMMAND) &&
      (answerMessageType != expectedAnswerMessageType))
    result = haier_protocol::HandlerError::UNSUPORTED_MESSAGE;
  if ((expectedPhase != ProtocolPhases::UNKNOWN) && (expectedPhase != this->protocol_phase_))
    result = haier_protocol::HandlerError::UNEXPECTED_MESSAGE;
  if (answerMessageType == (uint8_t)smartair2_protocol::FrameType::INVALID)
    result = haier_protocol::HandlerError::INVALID_ANSWER;
  return result;
}

haier_protocol::HandlerError HaierClimate::status_handler_(uint8_t requestType, uint8_t messageType,
                                                           const uint8_t *data, size_t dataSize) {
  haier_protocol::HandlerError result =
      this->answer_preprocess_(requestType, (uint8_t) smartair2_protocol::FrameType::CONTROL, messageType,
                               (uint8_t) smartair2_protocol::FrameType::STATUS, ProtocolPhases::UNKNOWN);
  if (result == haier_protocol::HandlerError::HANDLER_OK) {
    result = this->process_status_message_(data, dataSize);
    if (result != haier_protocol::HandlerError::HANDLER_OK) {
      ESP_LOGW(TAG, "Error %d while parsing Status packet", (int) result);
      this->set_phase_((this->protocol_phase_ >= ProtocolPhases::IDLE) ? ProtocolPhases::IDLE
                                                                       : ProtocolPhases::SENDING_FIRST_STATUS_REQUEST);
    } else {
      if (dataSize >= sizeof(smartair2_protocol::HaierPacketControl) + 2) {
        memcpy(this->last_status_message_.get(), data + 2, sizeof(smartair2_protocol::HaierPacketControl));
      } else {
        ESP_LOGW(TAG, "Status packet too small: %d (should be >= %d)", dataSize,
                 sizeof(smartair2_protocol::HaierPacketControl));
      }
      if (this->protocol_phase_ == ProtocolPhases::WAITING_FIRST_STATUS_ANSWER) {
        ESP_LOGI(TAG, "First HVAC status received");
        this->set_phase_(ProtocolPhases::IDLE);
      } else if (this->protocol_phase_ == ProtocolPhases::WAITING_STATUS_ANSWER) {
        this->set_phase_(ProtocolPhases::IDLE);
      } else if (this->protocol_phase_ == ProtocolPhases::WAITING_CONTROL_ANSWER) {
        this->set_phase_(ProtocolPhases::IDLE);
        this->force_send_control_ = false;
        if (this->hvac_settings_.valid)
          this->hvac_settings_.reset();
      }
    }
    return result;
  } else {
    this->set_phase_((this->protocol_phase_ >= ProtocolPhases::IDLE) ? ProtocolPhases::IDLE
                                                                     : ProtocolPhases::SENDING_FIRST_STATUS_REQUEST);
    return result;
  }
}

haier_protocol::HandlerError HaierClimate::timeout_default_handler_(uint8_t requestType) {
  ESP_LOGW(TAG, "Answer timeout for command %02X, phase %d", requestType, this->protocol_phase_);
  if (this->protocol_phase_ > ProtocolPhases::IDLE) {
    this->set_phase_(ProtocolPhases::IDLE);
  } else {
    this->set_phase_(ProtocolPhases::SENDING_FIRST_STATUS_REQUEST);
  }
  return haier_protocol::HandlerError::HANDLER_OK;
}

void HaierClimate::setup() {
  ESP_LOGI(TAG, "Haier initialization...");
  // Set timestamp here to give AC time to boot
  this->last_request_timestamp_ = std::chrono::steady_clock::now();
  this->set_phase_(ProtocolPhases::SENDING_FIRST_STATUS_REQUEST);
  // Set handlers
  this->haier_protocol_.set_answer_handler(
      (uint8_t)(smartair2_protocol::FrameType::CONTROL),
      std::bind(&HaierClimate::status_handler_, this, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3, std::placeholders::_4));
  this->haier_protocol_.set_default_timeout_handler(
      std::bind(&esphome::haier::HaierClimate::timeout_default_handler_, this, std::placeholders::_1));
}

void HaierClimate::dump_config() {
  LOG_CLIMATE("", "Haier smartAir2 Climate", this);
}

void HaierClimate::loop() {
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_valid_status_timestamp_).count() > 
      COMMUNICATION_TIMEOUT_MS) {
    if (this->protocol_phase_ > ProtocolPhases::IDLE) {
      // No status too long, reseting protocol
      ESP_LOGW(TAG, "Communication timeout, reseting protocol");
      this->last_valid_status_timestamp_ = now;
      this->force_send_control_ = false;
      if (this->hvac_settings_.valid)
        this->hvac_settings_.reset();
      this->set_phase_(ProtocolPhases::SENDING_FIRST_STATUS_REQUEST);
      return;
    } else {
      // No need to reset protocol if we didn't pass initialization phase
      this->last_valid_status_timestamp_ = now;
    }
  };
  if (this->hvac_settings_.valid || this->force_send_control_) {
    // If control message is pending we should send it ASAP unless we are in initialisation procedure or waiting for an
    // answer
    if ((this->protocol_phase_ == ProtocolPhases::IDLE) ||
        (this->protocol_phase_ == ProtocolPhases::SENDING_STATUS_REQUEST)) {
      ESP_LOGV(TAG, "Control packet is pending...");
      this->control_request_timestamp_ = now;
      this->set_phase_(ProtocolPhases::SENDING_CONTROL);
    }
  }
  switch (this->protocol_phase_) {
    case ProtocolPhases::SENDING_FIRST_STATUS_REQUEST:
    case ProtocolPhases::SENDING_STATUS_REQUEST:
      if (this->can_send_message() &&
          (std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_request_timestamp_).count() >
           DEFAULT_MESSAGES_INTERVAL_MS)) {
        static const haier_protocol::HaierMessage STATUS_REQUEST(
            (uint8_t) smartair2_protocol::FrameType::CONTROL, 0x4D01);
        this->send_message_(STATUS_REQUEST);
        this->last_status_request_ = now;
        this->set_phase_((ProtocolPhases)((uint8_t) this->protocol_phase_ + 1));
      }
      break;
    case ProtocolPhases::SENDING_CONTROL:
      if (this->control_called_) {
        this->control_request_timestamp_ = now;
        this->control_called_ = false;
      }
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - this->control_request_timestamp_).count() >
          CONTROL_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Sending control packet timeout!");
        this->force_send_control_ = false;
        if (this->hvac_settings_.valid)
          this->hvac_settings_.reset();
        this->forced_request_status_ = true;
        this->forced_publish_ = true;
        this->set_phase_(ProtocolPhases::IDLE);
      } else if (this->can_send_message() &&
                 (std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_request_timestamp_).count() >
                  CONTROL_MESSAGES_INTERVAL_MS))  // Using CONTROL_MESSAGES_INTERVAL_MS to speedup requests
      {
        haier_protocol::HaierMessage control_message = get_control_message_();
        this->send_message_(control_message);
        ESP_LOGI(TAG, "Control packet sent");
        this->set_phase_(ProtocolPhases::WAITING_CONTROL_ANSWER);
      }
      break;
    case ProtocolPhases::WAITING_FIRST_STATUS_ANSWER:
    case ProtocolPhases::WAITING_STATUS_ANSWER:
    case ProtocolPhases::WAITING_CONTROL_ANSWER:
      break;
    case ProtocolPhases::IDLE: {
      if (this->forced_request_status_ ||
          (std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_status_request_).count() >
           STATUS_REQUEST_INTERVAL_MS)) {
        this->set_phase_(ProtocolPhases::SENDING_STATUS_REQUEST);
          this->forced_request_status_ = false;
        }
    } break;
    default:
      // Shouldn't get here
      ESP_LOGE(TAG, "Wrong protocol handler state: %d, resetting communication", (int)this->protocol_phase_);
      this->set_phase_(ProtocolPhases::SENDING_FIRST_STATUS_REQUEST);
      break;
  }
  this->haier_protocol_.loop();
}

ClimateTraits HaierClimate::traits() { return traits_; }

void HaierClimate::control(const ClimateCall &call) {
  ESP_LOGD("Control", "Control call");
  if (this->protocol_phase_ < ProtocolPhases::IDLE) {
    ESP_LOGW(TAG, "Can't send control packet, first poll answer not received");
    return; //cancel the control, we cant do it without a poll answer.
  }
  if (this->hvac_settings_.valid) {
    ESP_LOGW(TAG, "Overriding old valid settings before they were applied!");
  }
  {
    if (call.get_mode().has_value())
      this->hvac_settings_.mode = call.get_mode();
    if (call.get_fan_mode().has_value())
      this->hvac_settings_.fan_mode = call.get_fan_mode();
    if (call.get_swing_mode().has_value())
      this->hvac_settings_.swing_mode =  call.get_swing_mode();
    if (call.get_target_temperature().has_value())
      this->hvac_settings_.target_temperature = call.get_target_temperature();
    if (call.get_preset().has_value())
      this->hvac_settings_.preset = call.get_preset();
    this->hvac_settings_.valid = true;
  }
  this->control_called_ = true;
}

haier_protocol::HaierMessage HaierClimate::get_control_message_() {
  uint8_t control_out_buffer[sizeof(smartair2_protocol::HaierPacketControl)];
  memcpy(control_out_buffer, this->last_status_message_.get(), sizeof(smartair2_protocol::HaierPacketControl));
  smartair2_protocol::HaierPacketControl *out_data = (smartair2_protocol::HaierPacketControl *) control_out_buffer;
  bool has_hvac_settings = false;
  if (this->hvac_settings_.valid) {
    has_hvac_settings = true;
    HvacSettings climate_control;
    climate_control = this->hvac_settings_;
    if (climate_control.mode.has_value()) {
      switch (climate_control.mode.value()) {
		case CLIMATE_MODE_OFF:
			out_data->ac_power = 0;
			break;

		case CLIMATE_MODE_AUTO:
			out_data->ac_power = 1;
			out_data->ac_mode = (uint8_t) smartair2_protocol::ConditioningMode::AUTO;
			out_data->fan_mode = other_modes_fan_speed_;
			break;

		case CLIMATE_MODE_HEAT:
			out_data->ac_power = 1;
			out_data->ac_mode = (uint8_t) smartair2_protocol::ConditioningMode::HEAT;
			out_data->fan_mode = other_modes_fan_speed_;
			break;

		case CLIMATE_MODE_DRY:
			out_data->ac_power = 1;
			out_data->ac_mode = (uint8_t) smartair2_protocol::ConditioningMode::DRY;
			out_data->fan_mode = other_modes_fan_speed_;
			break;

		case CLIMATE_MODE_FAN_ONLY:
			out_data->ac_power = 1;
			out_data->ac_mode = (uint8_t) smartair2_protocol::ConditioningMode::FAN;
			out_data->fan_mode = fan_mode_speed_;    // Auto doesn't work in fan only mode
			break;

		case CLIMATE_MODE_COOL:
			out_data->ac_power = 1;
			out_data->ac_mode = (uint8_t) smartair2_protocol::ConditioningMode::COOL;
			out_data->fan_mode = other_modes_fan_speed_;
			break;
		default:
			ESP_LOGE("Control", "Unsupported climate mode");
			break;
      }
    }
    //Set fan speed, if we are in fan mode, reject auto in fan mode
    if (climate_control.fan_mode.has_value()) {
      switch (climate_control.fan_mode.value()) {
		case CLIMATE_FAN_LOW:
			out_data->fan_mode = (uint8_t) smartair2_protocol::FanMode::FAN_LOW;
			break;
		case CLIMATE_FAN_MEDIUM:
			out_data->fan_mode = (uint8_t) smartair2_protocol::FanMode::FAN_MID;
			break;
		case CLIMATE_FAN_HIGH:
			out_data->fan_mode = (uint8_t) smartair2_protocol::FanMode::FAN_HIGH;
			break;
		case CLIMATE_FAN_AUTO:
			if (mode != CLIMATE_MODE_FAN_ONLY) //if we are not in fan only mode
				out_data->fan_mode = (uint8_t) smartair2_protocol::FanMode::FAN_AUTO;
			break;
		default:
			ESP_LOGE("Control", "Unsupported fan mode");
			break;
      }
    }
    //Set swing mode
    if (climate_control.swing_mode.has_value()) {
      switch (climate_control.swing_mode.value()) {
		case CLIMATE_SWING_OFF:
			out_data->use_swing_bits = 0;
			out_data->swing_both = 0;
			break;
		case CLIMATE_SWING_VERTICAL:
			out_data->swing_both = 0;
			out_data->vertical_swing = 1;
			out_data->horizontal_swing = 0;
			break;
		case CLIMATE_SWING_HORIZONTAL:
			out_data->swing_both = 0;
			out_data->vertical_swing = 0;
			out_data->horizontal_swing = 1;
			break;
		case CLIMATE_SWING_BOTH:
			out_data->swing_both = 1;
			out_data->use_swing_bits = 0;
			out_data->vertical_swing = 0;
			out_data->horizontal_swing = 0;
			break;
      }
    }
    if (climate_control.target_temperature.has_value()) {
      out_data->set_point =
          climate_control.target_temperature.value() - 16;  // set the temperature at our offset, subtract 16.
    }
  }
  out_data->display_status = this->display_status_ ? 1 : 0;
  return haier_protocol::HaierMessage((uint8_t) smartair2_protocol::FrameType::CONTROL, 0x4D5F,
                                      control_out_buffer, sizeof(smartair2_protocol::HaierPacketControl));
}

haier_protocol::HandlerError HaierClimate::process_status_message_(const uint8_t *packetBuffer, uint8_t size) {
  if (size < sizeof(smartair2_protocol::HaierStatus))
    return haier_protocol::HandlerError::WRONG_MESSAGE_STRUCTURE;
  smartair2_protocol::HaierStatus packet;
  memcpy(&packet, packetBuffer, size);
  bool should_publish = false;
  {
    // Target temperature
    float old_target_temperature = this->target_temperature;
    this->target_temperature = packet.control.set_point + 16.0f;
    should_publish = should_publish || (old_target_temperature != this->target_temperature);
  }
  {
    // Current temperature
    float old_current_temperature = this->current_temperature;
    this->current_temperature = packet.control.room_temperature;
    should_publish = should_publish || (old_current_temperature != this->current_temperature);
  }
  {
    // Fan mode
    optional<ClimateFanMode> old_fan_mode = this->fan_mode;
    //remember the fan speed we last had for climate vs fan
    if (packet.control.ac_mode == (uint8_t) smartair2_protocol::ConditioningMode::FAN) {
      this->fan_mode_speed_ = packet.control.fan_mode;
    } else {
      this->other_modes_fan_speed_ = packet.control.fan_mode;
    }
    switch (packet.control.fan_mode) {
    case (uint8_t)smartair2_protocol::FanMode::FAN_AUTO:
      this->fan_mode = CLIMATE_FAN_AUTO;
      break;
    case (uint8_t)smartair2_protocol::FanMode::FAN_MID:
      this->fan_mode = CLIMATE_FAN_MEDIUM;
      break;
    case (uint8_t)smartair2_protocol::FanMode::FAN_LOW:
      this->fan_mode = CLIMATE_FAN_LOW;
      break;
    case (uint8_t)smartair2_protocol::FanMode::FAN_HIGH:
      this->fan_mode = CLIMATE_FAN_HIGH;
      break;
    }
    should_publish = should_publish || (!old_fan_mode.has_value()) || (old_fan_mode.value() != fan_mode.value());
  }
  {
    // Display status
    // should be before "Climate mode" because it is changing this->mode
    if (packet.control.ac_power != 0) { 
      // if AC is off display status always ON so process it only when AC is on
      bool disp_status = packet.control.display_status != 0;
      if (disp_status != this->display_status_) { 
        // Do something only if display status changed
        if (this->mode == CLIMATE_MODE_OFF) {
          // AC just turned on from remote need to turn off display
          this->force_send_control_ = true;
        } else {
            this->display_status_ = disp_status;
        }
      }
    }
  }
  {
    // Climate mode
    ClimateMode old_mode = this->mode;
    if (packet.control.ac_power == 0) {
      this->mode = CLIMATE_MODE_OFF;
    } else {
      // Check current hvac mode
      switch (packet.control.ac_mode) {
      case (uint8_t)smartair2_protocol::ConditioningMode::COOL:
        this->mode = CLIMATE_MODE_COOL;
        break;
      case (uint8_t)smartair2_protocol::ConditioningMode::HEAT:
        this->mode = CLIMATE_MODE_HEAT;
        break;
      case (uint8_t)smartair2_protocol::ConditioningMode::DRY:
        this->mode = CLIMATE_MODE_DRY;
        break;
      case (uint8_t)smartair2_protocol::ConditioningMode::FAN:
        this->mode = CLIMATE_MODE_FAN_ONLY;
        break;
      case (uint8_t)smartair2_protocol::ConditioningMode::AUTO:
        this->mode = CLIMATE_MODE_AUTO;
        break;
      }
    }
    should_publish = should_publish || (old_mode != this->mode);
  }
  {
    // Swing mode
    ClimateSwingMode old_swing_mode = this->swing_mode;
    if (packet.control.swing_both == 0) {
        if (packet.control.vertical_swing != 0) {
            this->swing_mode = CLIMATE_SWING_VERTICAL;
        } else if (packet.control.horizontal_swing != 0) {
            this->swing_mode = CLIMATE_SWING_HORIZONTAL;
        } else {
            this->swing_mode = CLIMATE_SWING_OFF;
		}
    } else {
        swing_mode = CLIMATE_SWING_BOTH;
	}
    should_publish = should_publish || (old_swing_mode != this->swing_mode);
  }
  this->last_valid_status_timestamp_ = std::chrono::steady_clock::now();
  if (this->forced_publish_ || should_publish) {
#if (HAIER_LOG_LEVEL > 4)
    std::chrono::high_resolution_clock::time_point _publish_start = std::chrono::high_resolution_clock::now();
#endif
    this->publish_state();
#if (HAIER_LOG_LEVEL > 4)
    ESP_LOGV(TAG, "Publish delay: %lld ms",
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() -
                                                                   _publish_start)
                 .count());
#endif
    this->forced_publish_ = false;
  }
  if (should_publish) {
    ESP_LOGI(TAG, "HVAC values changed");
  }
  esp_log_printf_((should_publish ? ESPHOME_LOG_LEVEL_INFO : ESPHOME_LOG_LEVEL_DEBUG), TAG, __LINE__,
                  "HVAC Mode = 0x%X", packet.control.ac_mode);
  esp_log_printf_((should_publish ? ESPHOME_LOG_LEVEL_INFO : ESPHOME_LOG_LEVEL_DEBUG), TAG, __LINE__,
                  "Fan speed Status = 0x%X", packet.control.fan_mode);
  esp_log_printf_((should_publish ? ESPHOME_LOG_LEVEL_INFO : ESPHOME_LOG_LEVEL_DEBUG), TAG, __LINE__,
                  "Horizontal Swing Status = 0x%X", packet.control.horizontal_swing);
  esp_log_printf_((should_publish ? ESPHOME_LOG_LEVEL_INFO : ESPHOME_LOG_LEVEL_DEBUG), TAG, __LINE__,
                  "Vertical Swing Status = 0x%X", packet.control.vertical_swing);
  esp_log_printf_((should_publish ? ESPHOME_LOG_LEVEL_INFO : ESPHOME_LOG_LEVEL_DEBUG), TAG, __LINE__,
                  "Set Point Status = 0x%X", packet.control.set_point);
  return haier_protocol::HandlerError::HANDLER_OK;
}

void HaierClimate::send_message_(const haier_protocol::HaierMessage &command) {
  this->haier_protocol_.send_message(command, false);
  this->last_request_timestamp_ = std::chrono::steady_clock::now();
  ;
}

void HaierClimate::HvacSettings::reset() {
  this->valid = false;
  this->mode.reset();
  this->fan_mode.reset();
  this->swing_mode.reset();
  this->target_temperature.reset();
  this->preset.reset();
}

} // namespace haier
} // namespace esphome
