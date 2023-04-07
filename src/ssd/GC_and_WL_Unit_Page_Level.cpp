#include <math.h>
#include <vector>
#include <set>
#include "GC_and_WL_Unit_Page_Level.h"
#include "Flash_Block_Manager.h"
#include "FTL.h"

namespace SSD_Components
{

	GC_and_WL_Unit_Page_Level::GC_and_WL_Unit_Page_Level(const sim_object_id_type& id,
		Address_Mapping_Unit_Base* address_mapping_unit, Flash_Block_Manager_Base* block_manager, TSU_Base* tsu, NVM_PHY_ONFI* flash_controller, 
		GC_Block_Selection_Policy_Type block_selection_policy, double gc_threshold, bool preemptible_gc_enabled, double gc_hard_threshold,
		unsigned int ChannelCount, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
		unsigned int block_no_per_plane, unsigned int Page_no_per_block, unsigned int sectors_per_page, 
		bool use_copyback, double rho, SLC_Table* slc_table, unsigned int max_ongoing_gc_reqs_per_plane, bool dynamic_wearleveling_enabled, bool static_wearleveling_enabled, unsigned int static_wearleveling_threshold, int seed)
		: GC_and_WL_Unit_Base(id, address_mapping_unit, block_manager, tsu, flash_controller, block_selection_policy, gc_threshold, preemptible_gc_enabled, gc_hard_threshold,
		ChannelCount, chip_no_per_channel, die_no_per_chip, plane_no_per_die, block_no_per_plane, Page_no_per_block, sectors_per_page, use_copyback, rho, max_ongoing_gc_reqs_per_plane, 
			dynamic_wearleveling_enabled, static_wearleveling_enabled, static_wearleveling_threshold, seed, slc_table)
	{
		rga_set_size = (unsigned int)log2(block_no_per_plane);
	}
	
	//차후 수정 계획 - slc 영역이 줄어들수록 GC_is_in_urgent_mode의 return true; 빈도가 높아짐 => 성능 저하 야기 가능
	bool GC_and_WL_Unit_Page_Level::GC_is_in_urgent_mode(const NVM::FlashMemory::Flash_Chip* chip)
	{
		if (!preemptible_gc_enabled) {
			return true;
		}
		unsigned int block_pool_gc_hard_threshold_slc = (unsigned int)(gc_hard_threshold * (double)block_manager->getCurrSLCBlocksPerPlane());

		NVM::FlashMemory::Physical_Page_Address addr;
		addr.ChannelID = chip->ChannelID; addr.ChipID = chip->ChipID;
		for (unsigned int die_id = 0; die_id < die_no_per_chip; die_id++) {
			for (unsigned int plane_id = 0; plane_id < plane_no_per_die; plane_id++) {
				addr.DieID = die_id; addr.PlaneID = plane_id;
				//수정 - 23.03.23 SLC영역/TLC영역 둘 중 하나라도 pool size가 threshold 미만이면 수행
				if (block_manager->Get_pool_size(addr,true) < block_pool_gc_hard_threshold_slc
				||block_manager->Get_pool_size(addr,false) < block_pool_gc_hard_threshold)
					return true;
			}
		}

		return false;
	}

	/**
	 * victim block을 선정하여 erase transaction을 발행 및 Scheduling() 호출
	 * 수정계획 - SLC 영역에서 호출되었으면 slc_pool에 존재하는 블록 중에서 victim block을 선정하도록 변경
	 * 현재상황 - RGA 방식만 SLC 영역 반영
	 * 
	 * free_block_pool_size는 Get_free_block_pool_size(bool isSLC)가 들어옴
	*/
	void GC_and_WL_Unit_Page_Level::Check_gc_required(const unsigned int free_block_pool_size, const NVM::FlashMemory::Physical_Page_Address& plane_address, stream_id_type stream_id, bool is_slc)
	{
		PlaneBookKeepingType* pbke = block_manager->Get_plane_bookkeeping_entry(plane_address);
		flash_block_ID_type gc_candidate_block_id;
		//is_slc가 true인 경우는 Allocate block and page in plane for user write에서만 호출됨 
		if(is_slc) {
			//slc의 경우 data_wf_slc가 다 떨어진 경우에만 gc 수행
			if(pbke->Data_wf_slc[stream_id]==NULL){
				gc_candidate_block_id = pbke->slc_block_history.front(); //queue에서 지우는 건 마지막 조건까지 다 되는 경우
				if (pbke->Ongoing_erase_operations.size() >= max_ongoing_gc_reqs_per_plane)
					return;
			}
			else
				return;
		} else {
			if (free_block_pool_size < block_pool_gc_threshold) {
			//RGA 방식이기에 여기서 쓰이는 Get_coldest_block_id()는 의미가 없음
				gc_candidate_block_id = block_manager->Get_coldest_block_id(plane_address);

				if (pbke->Ongoing_erase_operations.size() >= max_ongoing_gc_reqs_per_plane) {
					return;
				}

				switch (block_selection_policy) {
					case SSD_Components::GC_Block_Selection_Policy_Type::GREEDY://Find the set of blocks with maximum number of invalid pages and no free pages
					{
						gc_candidate_block_id = 0;
						if (pbke->Ongoing_erase_operations.find(0) != pbke->Ongoing_erase_operations.end()) {
							gc_candidate_block_id++;
						}
						for (flash_block_ID_type block_id = 1; block_id < block_no_per_plane; block_id++) {
							if (pbke->Blocks[block_id].Invalid_page_count > pbke->Blocks[gc_candidate_block_id].Invalid_page_count
								&& pbke->Blocks[block_id].Current_page_write_index == pbke->Blocks[block_id].Last_page_index + 1 /*수정 - 23.03.03 pages_no_per_block*/
								&& is_safe_gc_wl_candidate(pbke, block_id)) {
								gc_candidate_block_id = block_id;
							}
						}
						break;
					}
					case SSD_Components::GC_Block_Selection_Policy_Type::RGA: //기본설정 - RGA 방식만 SLC 영역 반영
					{
						std::set<flash_block_ID_type> random_set;
						while (random_set.size() < rga_set_size) {
							unsigned int limit_inclusive = block_no_per_plane - 1;
							flash_block_ID_type block_id = random_generator.Uniform_uint(0, limit_inclusive);

							if (pbke->Ongoing_erase_operations.find(block_id) == pbke->Ongoing_erase_operations.end()
								&& is_safe_gc_wl_candidate(pbke, block_id)) {
								random_set.insert(block_id);
							}
							
						}
						gc_candidate_block_id = *random_set.begin();
						for(auto &block_id : random_set) {
							if (pbke->Blocks[block_id].Invalid_page_count > pbke->Blocks[gc_candidate_block_id].Invalid_page_count
								&& pbke->Blocks[block_id].Current_page_write_index == pbke->Blocks[block_id].Last_page_index + 1 /*수정- 23.03.03 pages_no_per_block*/) {
								gc_candidate_block_id = block_id;
							}
						}
						break;
					}
					case SSD_Components::GC_Block_Selection_Policy_Type::RANDOM:
					{
						gc_candidate_block_id = random_generator.Uniform_uint(0, block_no_per_plane - 1);
						unsigned int repeat = 0;

						//A write frontier block should not be selected for garbage collection
						while (!is_safe_gc_wl_candidate(pbke, gc_candidate_block_id) && repeat++ < block_no_per_plane) {
							gc_candidate_block_id = random_generator.Uniform_uint(0, block_no_per_plane - 1);
						}
						break;
					}
					case SSD_Components::GC_Block_Selection_Policy_Type::RANDOM_P:
					{
						gc_candidate_block_id = random_generator.Uniform_uint(0, block_no_per_plane - 1);
						unsigned int repeat = 0;

						//A write frontier block or a block with free pages should not be selected for garbage collection
						while ((pbke->Blocks[gc_candidate_block_id].Current_page_write_index < pbke->Blocks[gc_candidate_block_id].Last_page_index + 1 /*수정- 23.03.03 pages_no_per_block*/
								|| !is_safe_gc_wl_candidate(pbke, gc_candidate_block_id))
							&& repeat++ < block_no_per_plane) {
							gc_candidate_block_id = random_generator.Uniform_uint(0, block_no_per_plane - 1);
						}
						break;
					}
					case SSD_Components::GC_Block_Selection_Policy_Type::RANDOM_PP:
					{
						gc_candidate_block_id = random_generator.Uniform_uint(0, block_no_per_plane - 1);
						unsigned int repeat = 0;

						//The selected gc block should have a minimum number of invalid pages
						while ((pbke->Blocks[gc_candidate_block_id].Current_page_write_index < pbke->Blocks[gc_candidate_block_id].Last_page_index + 1 /*수정- 23.03.03 pages_no_per_block*/
							|| pbke->Blocks[gc_candidate_block_id].Invalid_page_count < random_pp_threshold
							|| !is_safe_gc_wl_candidate(pbke, gc_candidate_block_id))
							&& repeat++ < block_no_per_plane) {
							gc_candidate_block_id = random_generator.Uniform_uint(0, block_no_per_plane - 1);
						}
						break;
					}
					case SSD_Components::GC_Block_Selection_Policy_Type::FIFO:
						gc_candidate_block_id = pbke->Block_usage_history.front();
						pbke->Block_usage_history.pop();
						break;
					default:
						break;
				}
			}
			else //gc수행이 필요없는 경우
				return;
		}
		//This should never happen, but we check it here for safty
		if (pbke->Ongoing_erase_operations.find(gc_candidate_block_id) != pbke->Ongoing_erase_operations.end()) {
			PRINT_ERROR("GC operation has already operated on the block")
			return;
		}
		
		NVM::FlashMemory::Physical_Page_Address gc_candidate_address(plane_address);
		gc_candidate_address.BlockID = gc_candidate_block_id;
		Block_Pool_Slot_Type* block = &pbke->Blocks[gc_candidate_block_id];

		//No invalid page to erase
		if (block->Current_page_write_index == 0 || block->Invalid_page_count == 0) {
			return;
		}

		//slc 영역 gc를 수행하면 tlc block으로 추가되므로 tlc free block을 slc로 전환해서 slc영역에 추가해줘야 함
		if(is_slc) {
			if(ExecutionStatus::FAIL==block_manager->transformToSLCBlocks(pbke,1,dynamic_wearleveling_enabled))
				return; //여유가 없으므로 앞으로 들어오는 slc transaction은 FBM::Allocate block and page in plane for user write에서 tlc로 전환됨
			
			//다음 slc 트랜잭션 처리를 위해 Data_wf_slc 할당
			pbke->Data_wf_slc[stream_id] = (*pbke->free_slc_blocks.begin()).second;
			pbke->free_slc_blocks.erase(pbke->free_slc_blocks.begin());
			pbke->Data_wf_slc[stream_id]->Stream_id = stream_id;
			//gc를 수행할 수 있으므로 slc_block_history에서 삭제
			pbke->slc_block_history.pop();

			Stats::Total_slc_area_gc_executions++;
		}

		//Run the state machine to protect against race condition
		block_manager->GC_WL_started(gc_candidate_address); //plane_record->Blocks[block_address.BlockID].Has_ongoing_gc_wl = true;
		pbke->Ongoing_erase_operations.insert(gc_candidate_block_id);
		address_mapping_unit->Set_barrier_for_accessing_physical_block(gc_candidate_address);//Lock the block, so no user request can intervene while the GC is progressing
		
		//If there are ongoing requests targeting the candidate block, the gc execution should be postponed
		if (block_manager->Can_execute_gc_wl(gc_candidate_address)) { //해당 block에 ongoing user program count와 ongoing user read count가 0이어야 함
			Stats::Total_gc_executions++;
			//std::cout<<"Check_gc_required "<<plane_address.ChannelID<<"-"<<plane_address.ChipID<<"-"<<plane_address.DieID<<"-"<<plane_address.PlaneID<<", is slc: "<<is_slc<<std::endl;
			tsu->Prepare_for_transaction_submit();

			NVM_Transaction_Flash_ER* gc_erase_tr = new NVM_Transaction_Flash_ER(Transaction_Source_Type::GC_WL, pbke->Blocks[gc_candidate_block_id].Stream_id, gc_candidate_address, is_slc);
			//If there are some valid pages in block, then prepare flash transactions for page movement
			if (block->Current_page_write_index - block->Invalid_page_count > 0) {
				NVM_Transaction_Flash_RD* gc_read = NULL;
				NVM_Transaction_Flash_WR* gc_write = NULL;
				for (flash_page_ID_type pageID = 0; pageID < block->Current_page_write_index; pageID++) {
					if (block_manager->Is_page_valid(block, pageID)) {
						Stats::Total_page_movements_for_gc++;
						gc_candidate_address.PageID = pageID;

						//slc 영역인 경우 SLC trx 발행
						if (use_copyback) {
							gc_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, block->Stream_id, sector_no_per_page * SECTOR_SIZE_IN_BYTE,
								NO_LPA, address_mapping_unit->Convert_address_to_ppa(gc_candidate_address), NULL, 0, NULL, 0, INVALID_TIME_STAMP,false);
							gc_write->ExecutionMode = WriteExecutionModeType::COPYBACK;
							tsu->Submit_transaction(gc_write);
						} else {
							//gc_read는 slc 영역에 존재하는 경우 slc 속도로 읽지만 이후 전부 tlc로 기록되기에 false를 마지막 인자에 넣어줌
							gc_read = new NVM_Transaction_Flash_RD(Transaction_Source_Type::GC_WL, block->Stream_id, sector_no_per_page * SECTOR_SIZE_IN_BYTE,
								NO_LPA, address_mapping_unit->Convert_address_to_ppa(gc_candidate_address), gc_candidate_address, NULL, 0, NULL, 0, INVALID_TIME_STAMP,is_slc);
							gc_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, block->Stream_id, sector_no_per_page * SECTOR_SIZE_IN_BYTE,
								NO_LPA, NO_PPA, gc_candidate_address, NULL, 0, gc_read, 0, INVALID_TIME_STAMP,false);
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
}
