#ifndef _HAIER_AUTOMATION_H_
#define _HAIER_AUTOMATION_H_

#include "esphome/core/automation.h"
#include "haier_climate.h"

namespace esphome {
namespace haier {

template<typename... Ts> 
class DisplayOnAction : public Action<Ts...> 
{
public:
    DisplayOnAction(HaierClimate* parent) : parent_(parent) {}
    void play(Ts... x) 
    {
        this->parent_->set_display_state(true);
    }

protected:
    HaierClimate* parent_;
};

template<typename... Ts> 
class DisplayOffAction : public Action<Ts...> 
{
public:
    DisplayOffAction(HaierClimate* parent) : parent_(parent) {}
    void play(Ts... x) 
    {
        this->parent_->set_display_state(false);
    }

protected:
    HaierClimate* parent_;
};


}
}
#endif // _HAIER_AUTOMATION_H_
