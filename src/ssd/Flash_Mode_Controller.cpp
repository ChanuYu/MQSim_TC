#include "Flash_Mode_Controller.h"

namespace SSD_Components
{
    Flash_Mode_Controller::Flash_Mode_Controller(const sim_object_id_type& id, FTL* ftl,
			Address_Mapping_Unit_Base* address_mapping_unit, Flash_Block_Manager_Base* block_manager, TSU_Base* tsu, NVM_PHY_ONFI* flash_controller,
			unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
			unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int sector_no_per_page)
            : Sim_Object(id), address_mapping_unit(address_mapping_unit), block_manager(block_manager), tsu(tsu), flash_controller(flash_controller),
            channel_count(channel_count), chip_no_per_channel(chip_no_per_channel), die_no_per_chip(die_no_per_chip), plane_no_per_die(plane_no_per_die),
            block_no_per_plane(block_no_per_plane), pages_no_per_block(page_no_per_block), sector_no_per_page(sector_no_per_page)
            {}

    time_t Flash_Mode_Controller::getNextRequestTime()
    {
        return ftl->getNextRequestTime();
    }  


    void Flash_Mode_Controller::Start_simulation() {}
    void Flash_Mode_Controller::Validate_simulation_config() {}
    void Flash_Mode_Controller::Execute_simulator_event(MQSimEngine::Sim_Event* ev) {}
}

