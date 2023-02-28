#include "NextRequestTime.h"

namespace Host_Components
{
NextRequestTime::NextRequestTime(SSD_Components::FTL* ftl): ftl(ftl),nextTime(0)
{}

void NextRequestTime::setNextRequestTime(sim_time_type time)
{
    nextTime = time;
}

sim_time_type NextRequestTime::getNextRequestTime()
{
    return nextTime;
}

void NextRequestTime::setNextTimeToDevice(sim_time_type time)
{
    ftl->setNextRequestTime(time);
}
}

