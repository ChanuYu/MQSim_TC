#include "Flash_Mode_Controller.h"

namespace SSD_Components
{
    Flash_Mode_Controller::Flash_Mode_Controller(const sim_object_id_type& id, FTL* ftl,
			Address_Mapping_Unit_Base* address_mapping_unit, Flash_Block_Manager_Base* block_manager, TSU_Base* tsu, NVM_PHY_ONFI* flash_controller,
			unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
			unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int sector_no_per_page, unsigned int initial_slc_block_per_plane, bool consider_dynamic_wl)
            : Sim_Object(id), address_mapping_unit(address_mapping_unit), block_manager(block_manager), tsu(tsu), flash_controller(flash_controller),
            channel_count(channel_count), chip_no_per_channel(chip_no_per_channel), die_no_per_chip(die_no_per_chip), plane_no_per_die(plane_no_per_die),
            block_no_per_plane(block_no_per_plane), pages_no_per_block(page_no_per_block), sector_no_per_page(sector_no_per_page),initial_slc_block_per_plane(initial_slc_block_per_plane), consider_dynamic_wl(consider_dynamic_wl)
            {}

    time_t Flash_Mode_Controller::getNextRequestTime()
    {
        return ftl->getNextRequestTime();
    }  

    void Flash_Mode_Controller::Start_simulation()
    {
        //plane별로 slc 영역 확보
        std::multimap<unsigned int, Block_Pool_Slot_Type*>* p_free_blocks = NULL;
        PlaneBookKeepingType *p_pbk = NULL;
        Block_Pool_Slot_Type* p_block = NULL;
        
        for(unsigned int channel=0;channel<channel_count;channel++) {
            for(unsigned int chip = 0;chip<chip_no_per_channel;chip++) {
                for(unsigned int die = 0;die<die_no_per_chip;die++) {
                    for(unsigned int plane=0;plane<plane_no_per_die;plane++) {
                        NVM::FlashMemory::Physical_Page_Address addr(channel,chip,die,plane);
                        p_pbk = block_manager->Get_plane_bookkeeping_entry(addr);
                        
                        p_free_blocks = &p_pbk->Free_block_pool;

                        for(int i=0;i<initial_slc_block_per_plane;i++) {
                            p_block = (*p_free_blocks->begin()).second;
                            (*p_free_blocks).erase(p_free_blocks->begin());
                            
                            p_block->isSLC = true;
                            p_pbk->Add_to_free_block_pool(p_block,consider_dynamic_wl);
                        }
                        p_pbk->setNumOfSLCBlocks(initial_slc_block_per_plane);
                    }
                }
            }
        }
    }

    void Flash_Mode_Controller::Validate_simulation_config() {}
    
    void Flash_Mode_Controller::Execute_simulator_event(MQSimEngine::Sim_Event* ev) {}


    int increaseSLCBlocks(const NVM::FlashMemory::Physical_Page_Address&,int num_blocks)
    {

    }
    int decreaseSLCBlocks(const NVM::FlashMemory::Physical_Page_Address&,int num_blocks)
    {

    }

    void insertIntoSLCPool(PlaneBookKeepingType *);

    void adjustPageCountStatistics(PlaneBookKeepingType *, unsigned int num_changed_block); 






}

