#include "Flash_Mode_Controller.h"

namespace SSD_Components
{
    Flash_Mode_Controller::Flash_Mode_Controller(const sim_object_id_type& id, 
			Address_Mapping_Unit_Base* address_mapping_unit, Flash_Block_Manager_Base* block_manager, TSU_Base* tsu, NVM_PHY_ONFI* flash_controller,
			unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
			unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int sector_no_per_page)
            : MQSimEngine::Sim_Object("Flash_Mode_Controller")
            {}

    


    void Flash_Mode_Controller::Start_simulation() {}
    void Flash_Mode_Controller::Validate_simulation_config() {}
    void Flash_Mode_Controller::Execute_simulator_event(MQSimEngine::Sim_Event* ev) {}
}

