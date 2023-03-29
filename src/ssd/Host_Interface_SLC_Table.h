#ifndef HOST_INETERFACE_SLC_TABLE_H
#define HOST_INETERFACE_SLC_TABLE_H

#include "../nvm_chip/flash_memory/Physical_Page_Address.h"
#include "../nvm_chip/flash_memory/FlashTypes.h"

/**
 * 23.03.07
 * Host Interface에서 User Request를 transaction으로 쪼개어 transaction_list에 담을 때
 * 해당 트랜잭션의 블록이 SLC 블록인지 확인할 수 있도록 하기 위한 인터페이스
 * 
 * User_Request의 Stream_id, <static_cast>NVM_Transaction_Flash -> LPA 로 GetPPA() 호출 가능
 * 이때 NO_PPA인지 확인할 것
*/

//#include "Address_Mapping_Unit_Page_Level.h"
namespace SSD_Components
{
    class SLC_Table
    {
    public:
        SLC_Table(unsigned int total_stream, LPA_type capacity_per_stream);
        ~SLC_Table();

        bool isLPAEntrySLC(stream_id_type stream_id, const LPA_type &lpa);
        void changeEntryModeTo(stream_id_type stream_id, const LPA_type &lpa,Flash_Technology_Type type);
    private:
        LPA_type total_capacity_in_page_unit;
        unsigned int total_stream;
        bool **map_table;
    };
}



#endif