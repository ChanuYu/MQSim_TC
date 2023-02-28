#ifndef NEXT_REQUEST_TIME_H
#define NEXT_REQUEST_TIME_H

#include "../ssd/FTL.h"


namespace Host_Components
{
class NextRequestTime
{
public:
    NextRequestTime(SSD_Components::FTL*);
    void setNextRequestTime(sim_time_type);
    sim_time_type getNextRequestTime();
    void setNextTimeToDevice(sim_time_type);
private:
    SSD_Components::FTL *ftl;
    sim_time_type nextTime;
};
}




#endif