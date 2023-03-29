#include "Host_Interface_SLC_Table.h"

namespace SSD_Components
{
    SLC_Table::SLC_Table(unsigned int total_stream, LPA_type capacity_per_stream) : total_stream(total_stream)
    {
        map_table = new bool*[total_stream];
        for(unsigned int streamCnt=0;streamCnt<total_stream;streamCnt++)
            map_table[streamCnt] = new bool[capacity_per_stream]{false};
    }

    SLC_Table::~SLC_Table()
    {
        for(unsigned int i=0;i<total_stream;i++)
            delete[] map_table[i];
        delete[] map_table;
    }

    bool SLC_Table::isLPAEntrySLC(stream_id_type stream_id, const LPA_type &lpa)
    {
        return map_table[stream_id][lpa];
    }

    void SLC_Table::changeEntryModeTo(stream_id_type stream_id, const LPA_type &lpa,Flash_Technology_Type type)
    {
        switch(type)
        {
        case Flash_Technology_Type::SLC: //1
            map_table[stream_id][lpa] = true;
            break;
        case Flash_Technology_Type::TLC: //3
            map_table[stream_id][lpa] = false;
            break;
        }
    }
}