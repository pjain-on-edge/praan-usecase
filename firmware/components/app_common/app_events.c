#include "app_events.h"

static StaticEventGroup_t s_eg_buf;
static EventGroupHandle_t s_eg;

void app_events_init(void)
{
    s_eg = xEventGroupCreateStatic(&s_eg_buf);
}

EventGroupHandle_t app_events(void)
{
    return s_eg;
}
