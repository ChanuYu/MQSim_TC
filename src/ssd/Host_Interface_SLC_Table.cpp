#include "Host_Interface_SLC_Table.h"

namespace SSD_Components
{
    SLC_Table::SLC_Table(LPA_type total_capacity)
    {
        map_table = new bool[total_capacity]{false};
    }

    SLC_Table::~SLC_Table()
    {
        delete[] map_table;
    }

    bool SLC_Table::isLPAEntrySLC(const LPA_type &lpa)
    {
        return map_table[lpa];
    }

    void SLC_Table::changeEntryModeTo(const LPA_type &lpa,Flash_Technology_Type type)
    {
        switch(type)
        {
        case Flash_Technology_Type::SLC: //1
            map_table[lpa] = true;
            break;
        case Flash_Technology_Type::TLC: //3
            map_table[lpa] = false;
            break;
        }
    }
}