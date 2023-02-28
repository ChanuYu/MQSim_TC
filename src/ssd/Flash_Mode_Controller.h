#ifndef FLASH_MODE_CONTROLLER_H
#define FLASH_MODE_CONTROLLER_H

#include "../sim/Sim_Object.h"
#include "../nvm_chip/flash_memory/Flash_Chip.h"
#include "../nvm_chip/flash_memory/Physical_Page_Address.h"
#include "Address_Mapping_Unit_Base.h"
#include "Flash_Block_Manager_Base.h"
#include "TSU_Base.h"
#include "NVM_PHY_ONFI.h"

/**
 * 모듈설명: IDLE 상태일 때 (nextRequestTime 변수 참고) 
*/

namespace SSD_Components
{

    class Flash_Mode_Controller : public MQSimEngine::Sim_Object
    {
    public:
        Flash_Mode_Controller(const sim_object_id_type& id, 
			Address_Mapping_Unit_Base* address_mapping_unit, Flash_Block_Manager_Base* block_manager, TSU_Base* tsu, NVM_PHY_ONFI* flash_controller,
			unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
			unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int sector_no_per_page);

        void setNextRequestTime(time_t);
        time_t getNextRequestTime();


        void Start_simulation();
        void Validate_simulation_config();
        void Execute_simulator_event(MQSimEngine::Sim_Event*);

    private:
        static Flash_Mode_Controller * _my_instance;
        Address_Mapping_Unit_Base * address_mapping_unit;
        Flash_Block_Manager_Base* block_manager;
		TSU_Base* tsu;
		NVM_PHY_ONFI* flash_controller;

        time_t nextRequestTime;

        unsigned int channel_count;
		unsigned int chip_no_per_channel;
		unsigned int die_no_per_chip;
		unsigned int plane_no_per_die;
		unsigned int block_no_per_plane;
		unsigned int pages_no_per_block;
		unsigned int sector_no_per_page;
    };

} //end of SSD_Components




#endif //!FLASH_MODE_CONTROLLER_H