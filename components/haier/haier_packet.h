#ifndef HAIER_PACKET_H
#define HAIER_PACKET_H

enum ConditioningMode
{
    ConditioningAuto            = 0x00,
    ConditioningCool            = 0x01,
    ConditioningHeat            = 0x02,
    ConditioningFan             = 0x03,
    ConditioningDry             = 0x04
};

enum FanMode
{
    FanHigh                     = 0x00,
    FanMid                      = 0x01,
    FanLow                      = 0x02,
    FanAuto                     = 0x03
};

struct HaierPacketHeader
{
    // We skip start packet indication (0xFF 0xFF)
    /*  0 */    uint8_t             msg_length;                 // message length
    /*  1 */    uint8_t             reserved[6];                // 0x00 0x00 0x00 0x00 0x00 0x01
    /*  7 */    uint8_t             msg_type;                   // type of message
    /*  8 */    uint8_t             arguments[2];
};

struct HaierPacketControl
{
    // Control bytes starts here
    /* 10 */    uint8_t             :8;
    /* 11 */    uint8_t             room_temperature;           // current room temperature 1°C step
    /* 12 */    uint8_t             :8;
    /* 13 */    uint8_t             :8;
    /* 14 */    uint8_t             :8;
    /* 15 */    uint8_t             cntrl;                      // In AC => ESP packets - 0x7F, in ESP => AC packets - 0x00
    /* 16 */    uint8_t             :8;
    /* 17 */    uint8_t             :8;
    /* 18 */    uint8_t             :8;
    /* 19 */    uint8_t             :8;
    /* 20 */    uint8_t             :8;
    /* 21 */    uint8_t             ac_mode;                    // See enum ConditioningMode
    /* 22 */    uint8_t             :8;
    /* 23 */    uint8_t             fan_mode;                   // See enum FanMode
    /* 24 */    uint8_t             :8;
    /* 25 */    uint8_t             swing_both;                 // If 1 - swing both direction, if 0 - horizontal_swing and vertical_swing define vertical/horizontal/off
    /* 26 */    uint8_t             :7;
                uint8_t             lock_remote:1;              // Disable remote
    /* 27 */    uint8_t             ac_power:1;                 // Is ac on or off
                uint8_t             :2;
                uint8_t             health_mode:1;              // Health mode on or off
                uint8_t             compressor:1;               // Compressor on or off ???
                uint8_t             :0;
    /* 28 */    uint8_t             :8;
    /* 29 */    uint8_t             use_swing_bits:1;           // Indicate if horizontal_swing and vertical_swing should be used
                uint8_t             turbo_mode:1;               // Turbo mode
                uint8_t             disable_beeper:1;           // Silent mode
                uint8_t             horizontal_swing:1;         // Horizontal swing (if swing_both == 0)
                uint8_t             vertical_swing:1;           // Vertical swing (if swing_both == 0) if vertical_swing and horizontal_swing both 0 => swing off
                uint8_t             display_off:1;              // Led on or off
                uint8_t             :0;
    /* 30 */    uint8_t             :8;
    /* 31 */    uint8_t             :8;
    /* 32 */    uint8_t             :8;
    /* 33 */    uint8_t             set_point;                  // Target temperature with 16°C offset, 1°C step
};

struct HaierStatus
{
    HaierPacketHeader   header;
    HaierPacketControl  control;
};

struct HaierControl
{
    HaierPacketHeader   header;
    HaierPacketControl  control;
};

#define CONTROL_PACKET_SIZE         (sizeof(HaierPacketHeader) + sizeof(HaierPacketControl))
#define HEADER_SIZE                 (sizeof(HaierPacketHeader))

#endif // HAIER_PACKET_H