#include "Host_Interface_SLC_Table.h"

namespace SSD_Components
{
    SLC_Table::SLC_Table(LPA_type total_capacity)
    {
        table = new bool[total_capacity]{false};
    }

    SLC_Table::~SLC_Table()
    {
        delete[] table;
    }

    bool SLC_Table::isLPAInSLCBlock(const LPA_type &lpa)
    {
        return table[lpa];
    }
}