#include "Tiering_Area_Controller.h"
#include <cmath>

namespace SSD_Components
{
    
    Tiering_Area_Controller::Tiering_Area_Controller(GC_and_WL_Unit_Base *gc_wl, Address_Mapping_Unit_Base *amu, Flash_Block_Manager_Base *block_manager, TSU_Base *tsu, NVM_PHY_ONFI* flash_controller, Host_Interface_Base *hil,/*unsigned short *p_on_the_fly_requests,*/
                                    unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page)
                                    : Tiering_Area_Controller_Base(gc_wl,amu,block_manager,tsu,flash_controller,hil,/*p_on_the_fly_requests,*/channel,chip,die,plane,block,page) {}

    Tiering_Area_Controller::~Tiering_Area_Controller(){}

    unsigned int Tiering_Area_Controller::getCurrentSLCAreaSize(PlaneBookKeepingType *pbke)
    {
        return pbke->slc_blocks.size();
    }

    double Tiering_Area_Controller::getCurrentUtilization()
    {
        //return 1 -  (double)((pbke->Free_block_pool.size() + pbke->free_slc_blocks.size()) / block_per_plane);
        cur_util = 1 - (*p_cur_total_free_blocks / (double)total_block_counts);
        return cur_util;
    }

    void Tiering_Area_Controller::broadcastIdealSLCAreaSize(PlaneBookKeepingType *pbke)
    {

    }

    //calculateSpaceToBeAdjusted(util) 여기서 calculated_space에 결과값 저장
    //따라서, true를 반환할 경우 바로 adjustSLCArea 호출
    bool Tiering_Area_Controller::needToAdjustSLCArea()
    {
        double util = getCurrentUtilization();
        if(abs(calculateSpaceToBeAdjusted(util)) > ADJUST_THRESHOLD)
            return true;

        return false;
    }

    /**
     * TAC 모듈 동작 조건: on the fly requests가 없고, IDLE 상태의 칩이 존재해야 함
     * TAC 호출: 리퀘스트가 한동안 없을 때(tpcc-workload는 지금 간격이 작고 끊임없이 들어와서 소용 x),
     * 종석이형 피드백: 리퀘스트큐가 0일 때 처리하는 방식말고 write transaction이 끝날 때마다 수행하게 수정
     * 왜냐면 slc migration의 경우 당장 쓸 블록이 부족한 경우도 있을 것이기 때문에 조금씩 조금씩 검사하는 게 나을 것이라 함
    */
    void Tiering_Area_Controller::adjustSLCArea(unsigned int change_amount)
    {
        if(change_amount > 0)
            decreaseSLCArea(change_amount);
        else
            increaseSLCArea(change_amount);
    }

    void Tiering_Area_Controller::increaseSLCArea(unsigned int change_amount)
    {
        //byte단위의 공간을 블록단위로 치환

        //free block을 slc free block에 추가
    }

    void Tiering_Area_Controller::decreaseSLCArea(unsigned int change_amount)
    {
        //byte단위의 공간을 블록단위로 치환

        //블록수를 plane수로 고르게 나누기



    }

    bool Tiering_Area_Controller::needToMigrate(PlaneBookKeepingType *pbke)
    {

        return false;
    }

    //gc와 동일하게 작동
    void Tiering_Area_Controller::migrate(NVM::FlashMemory::Physical_Page_Address &block_address,PlaneBookKeepingType *pbke)
    {
        Block_Pool_Slot_Type* block = &pbke->Blocks[block_address.BlockID];
        flash_block_ID_type block_id = block->BlockID;

        if (block->Current_page_write_index == 0 || block->Invalid_page_count == 0) {
			return;
		}

        //Run the state machine to protect against race condition
		block_manager->GC_WL_started(block_address); //plane_record->Blocks[block_address.BlockID].Has_ongoing_gc_wl = true;
		pbke->Ongoing_erase_operations.insert(block_id);
		_my_instance->amu->Set_barrier_for_accessing_physical_block(block_address);//Lock the block, so no user request can intervene while the GC is progressing
		
        pbke->slc_block_history.pop();
        pbke->slc_blocks.erase(block_id);

        //If there are ongoing requests targeting the candidate block, the gc execution should be postponed
		if (block_manager->Can_execute_gc_wl(block_address)) { //해당 block에 ongoing user program count와 ongoing user read count가 0이어야 함
			Stats::Total_gc_executions++;
			//std::cout<<"Check_gc_required "<<plane_address.ChannelID<<"-"<<plane_address.ChipID<<"-"<<plane_address.DieID<<"-"<<plane_address.PlaneID<<", is slc: "<<is_slc<<std::endl;
			tsu->Prepare_for_transaction_submit();

			NVM_Transaction_Flash_ER* gc_erase_tr = new NVM_Transaction_Flash_ER(Transaction_Source_Type::MIGRATION, pbke->Blocks[block_id].Stream_id, block_address, true);
			//If there are some valid pages in block, then prepare flash transactions for page movement
			if (block->Current_page_write_index - block->Invalid_page_count > 0) {
				NVM_Transaction_Flash_RD* gc_read = NULL;
				NVM_Transaction_Flash_WR* gc_write = NULL;
				for (flash_page_ID_type pageID = 0; pageID < block->Current_page_write_index; pageID++) {
					if (block_manager->Is_page_valid(block, pageID)) {
						Stats::Total_page_movements_for_gc++;
						block_address.PageID = pageID;

						if (_my_instance->gc_wl->use_copyback) {
							gc_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::MIGRATION, block->Stream_id, _my_instance->gc_wl->sector_no_per_page * SECTOR_SIZE_IN_BYTE,
								NO_LPA, amu->Convert_address_to_ppa(block_address), NULL, 0, NULL, 0, INVALID_TIME_STAMP,false);
							gc_write->ExecutionMode = WriteExecutionModeType::COPYBACK;
							tsu->Submit_transaction(gc_write);
						} else {
							//gc_read는 slc 영역에 존재하는 경우 slc 속도로 읽지만 이후 전부 tlc로 기록되기에 false를 마지막 인자에 넣어줌
							gc_read = new NVM_Transaction_Flash_RD(Transaction_Source_Type::MIGRATION, block->Stream_id, _my_instance->gc_wl->sector_no_per_page * SECTOR_SIZE_IN_BYTE,
								NO_LPA, amu->Convert_address_to_ppa(block_address), block_address, NULL, 0, NULL, 0, INVALID_TIME_STAMP,true);
							gc_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, block->Stream_id, _my_instance->gc_wl->sector_no_per_page * SECTOR_SIZE_IN_BYTE,
								NO_LPA, NO_PPA, block_address, NULL, 0, gc_read, 0, INVALID_TIME_STAMP,false);
							gc_write->ExecutionMode = WriteExecutionModeType::SIMPLE;
							gc_write->RelatedErase = gc_erase_tr;
							gc_read->RelatedWrite = gc_write;
							tsu->Submit_transaction(gc_read);//Only the read transaction would be submitted. The Write transaction is submitted when the read transaction is finished and the LPA of the target page is determined
						}
						gc_erase_tr->Page_movement_activities.push_back(gc_write);
					}
				}
			}
			block->Erase_transaction = gc_erase_tr;
			tsu->Submit_transaction(gc_erase_tr);

			tsu->Schedule();
		}
    }

    //amu에 요청하면 amu에서 처리
    void Tiering_Area_Controller::getVictimPages(std::vector<LPA_type> &victim_pages, PlaneBookKeepingType *pbke)
    {

    }

    void Tiering_Area_Controller::executeHotdataTiering(NVM::FlashMemory::Flash_Chip *chip)
    {

    }
    void Tiering_Area_Controller::executeCompression(NVM::FlashMemory::Flash_Chip *chip)
    {
        
    }

    //byte 단위로 반환
    int Tiering_Area_Controller::calculateSpaceToBeAdjusted(double util)
    {
        unsigned int ideal_tlc_size = getIdealTLCAreaSize(util);
        unsigned int current_tlc_size = (*p_cur_tlc_free_blocks) << (ilogb(page_per_block) + ilogb(PAGE_SIZE));
        
        //압축을 통해 새로 창출한 페이지 수를 기록한다고 가정
        unsigned int cumulative_compression_size = (*p_cumulative_compression) << ilogb(PAGE_SIZE);
        
        calculated_space = ideal_tlc_size - current_tlc_size - cumulative_compression_size;
        return calculated_space;
    }


    double Tiering_Area_Controller::getIdealSLCRatio(double util)
    {
        unsigned int percentage = (unsigned int)(util * 100);
        percentage /= 5;

        switch(percentage)
        {
            case 0: case 1: case 2: case 3: case 4: case 5:
                return 0.14;
            case 6: case 7:
                return 0.127;
            case 8: case 9:
                return 0.102;
            case 10: case 11:
                return 0.076;
            case 12: case 13:
                return 0.05;
            case 14: case 15:
                return 0.025;
            case 16: case 17:
                return 0.012;
            default:
                return 0.0; 
        }   
    }

    //용량으로 계산해야 함
    unsigned int Tiering_Area_Controller::getIdealSLCAreaSize(double util)
    {
        return (unsigned int)(getIdealSLCRatio(util) * total_capacity);
    }
    
    unsigned long Tiering_Area_Controller::getIdealTLCAreaSize(double util)
    {
        //return total_capacity - 3 * getIdealSLCAreaSize(util);
        return total_capacity * (1.0 - 3 * getIdealSLCRatio(util));
    }
}