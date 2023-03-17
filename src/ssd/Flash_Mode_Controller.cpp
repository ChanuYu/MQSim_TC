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

        int res, decreased_pages;
        
        for(unsigned int channel=0;channel<channel_count;channel++) {
            for(unsigned int chip = 0;chip<chip_no_per_channel;chip++) {
                for(unsigned int die = 0;die<die_no_per_chip;die++) {
                    for(unsigned int plane=0;plane<plane_no_per_die;plane++) {
                        NVM::FlashMemory::Physical_Page_Address addr(channel,chip,die,plane);
                        p_pbk = block_manager->Get_plane_bookkeeping_entry(addr);
                        res = increaseSLCBlocks(p_pbk,initial_slc_block_per_plane);
                        decreased_pages = calculateNumChangedPages(res);
                        adjustPageCountStatistics(p_pbk,decreased_pages,TransitionType::TLCTOSLC);
                    }
                }
            }
        }
    }

    void Flash_Mode_Controller::Validate_simulation_config() {}

    void Flash_Mode_Controller::Execute_simulator_event(MQSimEngine::Sim_Event* ev) {}


    /**
     * 수정계획 - FreeBlock이 부족한 경우에 어떻게 변경하여 가져올 것인지 구상
    */
    int Flash_Mode_Controller::increaseSLCBlocks(PlaneBookKeepingType *p_pbk,int num_blocks)
    {
        Block_Pool_Slot_Type* p_block = NULL;
        std::multimap<unsigned int, Block_Pool_Slot_Type*>* p_free_blocks = NULL;

        p_free_blocks = &p_pbk->Free_block_pool;

        for(int i=0;i<num_blocks;i++) {

            if(p_free_blocks->begin()==p_free_blocks->end())
            {
                std::cout<<"There's no free block in TLC area"<<std::endl;
                return -1;
            }

            /**
             * FreeBlock이 부족한 경우 사용 중인 블록에서 valid page 존재하는지 확인 후 이전하는 작업(Erase()호출 포함) 추가해야 함
            */

            p_block = (*p_free_blocks->begin()).second;
            (*p_free_blocks).erase(p_free_blocks->begin());
            transformToSLC(p_block);
            p_pbk->Add_to_free_block_pool(p_block,consider_dynamic_wl);
        }

        int num_slc_blocks = p_pbk->getCurNumOfSLCBlocks() + num_blocks;
        p_pbk->setNumOfSLCBlocks(num_slc_blocks);

        return num_blocks; //바뀐 블록 수 반환
    }

    //블록에 쓸 수 있는 페이지 수 조정 + plane의 페이지 수 조정
    void Flash_Mode_Controller::transformToSLC(Block_Pool_Slot_Type *blk)
    {
        blk->isSLC = true;
        blk->Last_page_index = pages_no_per_block / 3;
    }

    //미완성
    int Flash_Mode_Controller::decreaseSLCBlocks(PlaneBookKeepingType *p_pbk,int num_blocks)
    {
        Block_Pool_Slot_Type* p_block = NULL;
        std::multimap<unsigned int, Block_Pool_Slot_Type*>* p_free_blocks = NULL;

        p_free_blocks = &p_pbk->Free_block_pool;

        return num_blocks; //바뀐 블록 수 반환
    }

    void Flash_Mode_Controller::insertIntoSLCPool(PlaneBookKeepingType *p_pbk)
    {

    }

    //내용물을 다 비운 뒤 플래시모드를 전환한다고 가정하고 페이지 변동 반영
    inline int Flash_Mode_Controller::calculateNumChangedPages(int num_blocks)
    {
        return (pages_no_per_block / 3) * 2 * num_blocks;
    }

    /**
     * SLCTOTLC의 경우 늘어나야 할 페이지 수가,
     * TLCTOSLC의 경우 줄어들어야 할 페이지 수가 num_pages로 들어옴
     * SLC-TLC 전환으로 인한 페이지 수의 변화를 Invalid page로 표시하는 것으로 가정 <- 차후 방법이 바뀔 경우 수정 필요
    */
    void Flash_Mode_Controller::adjustPageCountStatistics(PlaneBookKeepingType *p_pbk, int num_pages, TransitionType type)
    {
    /*
        std::cout<<"Total pages: "<<p_pbk->Total_pages_count<<std::endl;
        std::cout<<"Free pages: "<<p_pbk->Free_pages_count<<std::endl;
        std::cout<<"Valid pages: "<<p_pbk->Valid_pages_count<<std::endl;
        std::cout<<"Invalid pages: "<<p_pbk->Invalid_pages_count<<std::endl;
    */
        if(type==TransitionType::TLCTOSLC)
            num_pages *= -1;
        
        p_pbk->Total_pages_count += num_pages;
        p_pbk->Free_pages_count += num_pages;
    }
}

