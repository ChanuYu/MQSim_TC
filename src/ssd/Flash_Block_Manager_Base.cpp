#include "Flash_Block_Manager.h"


namespace SSD_Components
{
	unsigned int initial_pages_per_blk;
	unsigned int Block_Pool_Slot_Type::Page_vector_size = 0;
	Flash_Block_Manager_Base::Flash_Block_Manager_Base(GC_and_WL_Unit_Base* gc_and_wl_unit, unsigned int max_allowed_block_erase_count, unsigned int total_concurrent_streams_no,
		unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
		unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int initial_slc_blk)
		: gc_and_wl_unit(gc_and_wl_unit), max_allowed_block_erase_count(max_allowed_block_erase_count), total_concurrent_streams_no(total_concurrent_streams_no),
		channel_count(channel_count), chip_no_per_channel(chip_no_per_channel), die_no_per_chip(die_no_per_chip), plane_no_per_die(plane_no_per_die),
		block_no_per_plane(block_no_per_plane), pages_no_per_block(page_no_per_block), initial_slc_blk_per_plane(initial_slc_blk), current_slc_blocks_per_plane(initial_slc_blk)
	{
		//23.03.03
		initial_pages_per_blk = pages_no_per_block;

		plane_manager = new PlaneBookKeepingType***[channel_count];
		for (unsigned int channelID = 0; channelID < channel_count; channelID++) {
			plane_manager[channelID] = new PlaneBookKeepingType**[chip_no_per_channel];
			for (unsigned int chipID = 0; chipID < chip_no_per_channel; chipID++) {
				plane_manager[channelID][chipID] = new PlaneBookKeepingType*[die_no_per_chip];
				for (unsigned int dieID = 0; dieID < die_no_per_chip; dieID++) {
					plane_manager[channelID][chipID][dieID] = new PlaneBookKeepingType[plane_no_per_die];

					//Initialize plane book keeping data structure
					for (unsigned int planeID = 0; planeID < plane_no_per_die; planeID++) {
						plane_manager[channelID][chipID][dieID][planeID].Total_pages_count = block_no_per_plane * pages_no_per_block;
						plane_manager[channelID][chipID][dieID][planeID].Free_pages_count = block_no_per_plane * pages_no_per_block;
						plane_manager[channelID][chipID][dieID][planeID].Valid_pages_count = 0;
						plane_manager[channelID][chipID][dieID][planeID].Invalid_pages_count = 0;
						plane_manager[channelID][chipID][dieID][planeID].Ongoing_erase_operations.clear();
						plane_manager[channelID][chipID][dieID][planeID].Blocks = new Block_Pool_Slot_Type[block_no_per_plane];
						
						//Initialize block pool for plane
						for (unsigned int blockID = 0; blockID < block_no_per_plane; blockID++) {
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].BlockID = blockID;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Current_page_write_index = 0;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Current_status = Block_Service_Status::IDLE;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Invalid_page_count = 0;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Erase_count = 0;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Holds_mapping_data = false;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Has_ongoing_gc_wl = false;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Erase_transaction = NULL;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Ongoing_user_program_count = 0;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Ongoing_user_read_count = 0;
							
							//수정 - 23.03.03
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].isSLC = false;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Last_page_index = pages_no_per_block - 1;

							//uint64_t => 변수 하나당 64개의 페이지 표현 가능, 블록내에 256개의 페이지가 존재하므로 Page_vector_size는 4
							Block_Pool_Slot_Type::Page_vector_size = pages_no_per_block / (sizeof(uint64_t) * 8) + (pages_no_per_block % (sizeof(uint64_t) * 8) == 0 ? 0 : 1);
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Invalid_page_bitmap = new uint64_t[Block_Pool_Slot_Type::Page_vector_size];
							for (unsigned int i = 0; i < Block_Pool_Slot_Type::Page_vector_size; i++) {
								plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Invalid_page_bitmap[i] = All_VALID_PAGE;
							}
							plane_manager[channelID][chipID][dieID][planeID].Add_to_free_block_pool(&plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID], false);
						}

						//plane별로 초기 free SLC 블록 할당
						transformToSLCBlocks(&plane_manager[channelID][chipID][dieID][planeID], initial_slc_blk_per_plane, true);

						plane_manager[channelID][chipID][dieID][planeID].Data_wf = new Block_Pool_Slot_Type*[total_concurrent_streams_no];
						plane_manager[channelID][chipID][dieID][planeID].Translation_wf = new Block_Pool_Slot_Type*[total_concurrent_streams_no];
						plane_manager[channelID][chipID][dieID][planeID].GC_wf = new Block_Pool_Slot_Type*[total_concurrent_streams_no];

						//수정 - 23.03.15
						plane_manager[channelID][chipID][dieID][planeID].Data_wf_slc = new Block_Pool_Slot_Type*[total_concurrent_streams_no];
						//plane_manager[channelID][chipID][dieID][planeID].GC_wf_slc = new Block_Pool_Slot_Type*[total_concurrent_streams_no];
						plane_manager[channelID][chipID][dieID][planeID].latest_data_wf_slc = NULL;
						for (unsigned int stream_cntr = 0; stream_cntr < total_concurrent_streams_no; stream_cntr++) {
							plane_manager[channelID][chipID][dieID][planeID].Data_wf[stream_cntr] = plane_manager[channelID][chipID][dieID][planeID].Get_a_free_block(stream_cntr, false);
							plane_manager[channelID][chipID][dieID][planeID].Translation_wf[stream_cntr] = plane_manager[channelID][chipID][dieID][planeID].Get_a_free_block(stream_cntr, true);
							plane_manager[channelID][chipID][dieID][planeID].GC_wf[stream_cntr] = plane_manager[channelID][chipID][dieID][planeID].Get_a_free_block(stream_cntr, false);

							plane_manager[channelID][chipID][dieID][planeID].Data_wf_slc[stream_cntr] = plane_manager[channelID][chipID][dieID][planeID].Get_a_free_block(stream_cntr, false, true);
							//plane_manager[channelID][chipID][dieID][planeID].GC_wf_slc[stream_cntr] = plane_manager[channelID][chipID][dieID][planeID].Get_a_free_block(stream_cntr, false, true);
						}
					}
				}
			}
		}
	}

	Flash_Block_Manager_Base::~Flash_Block_Manager_Base() 
	{
		for (unsigned int channel_id = 0; channel_id < channel_count; channel_id++) {
			for (unsigned int chip_id = 0; chip_id < chip_no_per_channel; chip_id++) {
				for (unsigned int die_id = 0; die_id < die_no_per_chip; die_id++) {
					for (unsigned int plane_id = 0; plane_id < plane_no_per_die; plane_id++) {
						for (unsigned int blockID = 0; blockID < block_no_per_plane; blockID++) {
							delete[] plane_manager[channel_id][chip_id][die_id][plane_id].Blocks[blockID].Invalid_page_bitmap;
						}
						delete[] plane_manager[channel_id][chip_id][die_id][plane_id].Blocks;
						delete[] plane_manager[channel_id][chip_id][die_id][plane_id].GC_wf;
						delete[] plane_manager[channel_id][chip_id][die_id][plane_id].Data_wf;
						delete[] plane_manager[channel_id][chip_id][die_id][plane_id].Translation_wf;
					}
					delete[] plane_manager[channel_id][chip_id][die_id];
				}
				delete[] plane_manager[channel_id][chip_id];
			}
			delete[] plane_manager[channel_id];
		}
		delete[] plane_manager;
	}

	void Flash_Block_Manager_Base::Set_GC_and_WL_Unit(GC_and_WL_Unit_Base* gcwl)
	{
		this->gc_and_wl_unit = gcwl;
	}

	void Flash_Block_Manager_Base::setFlashModeController(Flash_Mode_Controller* fmc)
	{
		this->FMC = fmc;
	}

	//FTL에서 관리하는 개별 피지컬 블록에 대한 메타 데이터 초기화
	void Block_Pool_Slot_Type::Erase()
	{
		Current_page_write_index = 0;
		Invalid_page_count = 0;
		Erase_count++;
		for (unsigned int i = 0; i < Block_Pool_Slot_Type::Page_vector_size; i++) {
			Invalid_page_bitmap[i] = All_VALID_PAGE;
		}
		Stream_id = NO_STREAM;
		Holds_mapping_data = false;
		Erase_transaction = NULL;

		//23.03.03
		isSLC = false;
		Last_page_index = initial_pages_per_blk - 1;
	}

	//interface for free_slc_blocks <---> Data(GC)_wf_slc
	Block_Pool_Slot_Type* PlaneBookKeepingType::Get_a_free_block(stream_id_type stream_id, bool for_mapping_data, bool for_slc)
	{
		Block_Pool_Slot_Type* new_block = NULL;
		
		//SLC 영역 내에서의 GC가 구현될 때까지 보류
		std::multimap<unsigned int, Block_Pool_Slot_Type*> &free_block_pool = for_slc ? free_slc_blocks : Free_block_pool;
		//std::multimap<unsigned int, Block_Pool_Slot_Type*> &free_block_pool = Free_block_pool;
		if (free_block_pool.size() == 0) {
			if(for_slc)
				return NULL;
			else
				PRINT_ERROR("Requesting a free block from an empty pool!")
		}
		new_block = (*free_block_pool.begin()).second;//Assign a new write frontier block
		free_block_pool.erase(free_block_pool.begin());
		new_block->Stream_id = stream_id;
		new_block->Holds_mapping_data = for_mapping_data;

		if(for_slc)
			slc_block_history.push(new_block->BlockID);
		else
			Block_usage_history.push(new_block->BlockID);

		return new_block;
	}

	//Free_block_pool <---> free_slc_block 전용 인터페이스 (Free block에서만 호출되어야 함)
	//slc ---> tlc인 경우에도 free_slc_blocks에 있는 블록들을 대상으로 Free_block_pool로 이동
	//free_slc_block에 잔여블록이 부족한 경우 transformToTLC()에서 migration함수를 호출한 후에 다시 시도해야 함
	//Block의 isSLC, Last_page_index, Invalid_page_count, bitmap 조정
	void Block_Pool_Slot_Type::changeFlashMode(bool toSLC)
	{
		isSLC = toSLC;
		Last_page_index = toSLC ? initial_pages_per_blk / 3 - 1 : initial_pages_per_blk - 1;
		unsigned int _invalid_page_count = (initial_pages_per_blk / 3) * 2 + 1;

		if(toSLC)
		{
			Invalid_page_count += _invalid_page_count;

			//invalidate page bitmap
			for(unsigned int i=Last_page_index+1;i<initial_pages_per_blk;i++)
				Invalid_page_bitmap[i / 64] |= ((uint64_t)0x1) << (i % 64);
		}
		else
		{
			Invalid_page_count -= _invalid_page_count;

			for(unsigned int i=0;i<Block_Pool_Slot_Type::Page_vector_size;i++)
				Invalid_page_bitmap[i] = All_VALID_PAGE;
		}
			
	}

	void PlaneBookKeepingType::Check_bookkeeping_correctness(const NVM::FlashMemory::Physical_Page_Address& plane_address)
	{
		if (Total_pages_count != Free_pages_count + Valid_pages_count + Invalid_pages_count) {
			PRINT_ERROR("Inconsistent status in the plane bookkeeping record!")
		}
		if (Free_pages_count == 0) {
			PRINT_ERROR("Plane " << "@" << plane_address.ChannelID << "@" << plane_address.ChipID << "@" << plane_address.DieID << "@" << plane_address.PlaneID << " pool size: " << Get_free_block_pool_size() << " ran out of free pages! Bad resource management! It is not safe to continue simulation!");
		}
	}

	/**
	 * GC_WL_Unit_Page_Level::Check_gc_required()와 함께 쓰임
	 * Check_gc_required()가 호출될 때 bool slc_area를 추가하고 slc 영역 여부에 따라 반환하는 사이즈를 달리할 것
	*/
	unsigned int PlaneBookKeepingType::Get_free_block_pool_size(bool slc_area)
	{
		const std::multimap<unsigned int, Block_Pool_Slot_Type*> &free_block_pool = slc_area ? free_slc_blocks : Free_block_pool;
		return (unsigned int)free_block_pool.size();
	}

	//Erased block을 Free block pool에 추가하는 경우, 초기화에서 free block 추가하는 경우에 호출
	//Free_block_pool <---> free_slc_block 인터페이스 이전단계
	void PlaneBookKeepingType::Add_to_free_block_pool(Block_Pool_Slot_Type* block, bool consider_dynamic_wl)
	{
		if (consider_dynamic_wl) {
			std::pair<unsigned int, Block_Pool_Slot_Type*> entry(block->Erase_count, block);
			Free_block_pool.insert(entry);
		} else {
			std::pair<unsigned int, Block_Pool_Slot_Type*> entry(0, block);
			Free_block_pool.insert(entry);
		} 
	}

	void PlaneBookKeepingType::setNumOfSLCBlocks(unsigned int num)
	{
		curNumOfSLCBlocks = num;
	}

	unsigned int PlaneBookKeepingType::getCurNumOfSLCBlocks()
	{
		return curNumOfSLCBlocks;
	}

	bool PlaneBookKeepingType::isBlockInSLCPool(flash_block_ID_type block_id)
	{
		return slc_blocks.find(block_id) != slc_blocks.end();
	}

	//Free_block_pool <---> free_slc_blocks => FBM::transformToSLCBlocks()
	//plane별로 slc로 전환해야 할 블록의 수를 넘겨주면 각종 변수 조정 및 free_slc_block 풀로 이동
	//호출시기: 미정
	ExecutionStatus Flash_Block_Manager_Base::transformToSLCBlocks(PlaneBookKeepingType *pbke, unsigned int num, bool consider_dynamic_wl)
	{
		if(pbke->Free_block_pool.size() < num) {
			//Tiering Area Controller에서 slc영역 데이터를 demote하고 영역을 줄이는 함수 호출하도록 구현 필요

			return ExecutionStatus::FAIL; //GC에서 별도로 처리하지 않고 대기
		}

		Block_Pool_Slot_Type *block = NULL;
		unsigned int erase_count;
		for(unsigned int i=0;i<num;i++){
			block = (*pbke->Free_block_pool.begin()).second;
			block->changeFlashMode(true); //Last_page_index, page bit map 변경

			erase_count = consider_dynamic_wl ? block->Erase_count : 0;
			pbke->free_slc_blocks.insert(std::pair<unsigned int, Block_Pool_Slot_Type*>(erase_count, block));
			pbke->Free_block_pool.erase(pbke->Free_block_pool.begin());

			//slc pool에 추가
			pbke->slc_blocks.insert(std::pair<flash_block_ID_type,Block_Pool_Slot_Type*>(block->BlockID,block));
		}

		pbke->Free_pages_count -= num * pages_no_per_block;
		pbke->Invalid_pages_count += num * pages_no_per_block;
		pbke->setNumOfSLCBlocks(pbke->getCurNumOfSLCBlocks() + num);

		return ExecutionStatus::SUCCESS;
	}

	//Free_block_pool <---> free_slc_blocks => FBM::transformToSLCBlocks()
	//SLC block의 내용을 erase 하는 것은 별도의 함수
	ExecutionStatus Flash_Block_Manager_Base::transformToTLCBlocks(PlaneBookKeepingType *pbke, unsigned int num, bool consider_dynamic_wl)
	{
		if(pbke->free_slc_blocks.size() < num)
		{
			//pbke->slc_blocks에서 잘 안 쓰이는 데이터 마이그레이션 후 free_slc_blocks로 추가하는 함수 호출
			//event 재등록
			return ExecutionStatus::FAIL; //아직 migration이 완료되지 않았으므로 event를 재등록하고 return
		}

		std::map<flash_block_ID_type,Block_Pool_Slot_Type*>::iterator iter;
		Block_Pool_Slot_Type *block = NULL;
		unsigned int erase_count;
		for(unsigned int i=0;i<num;i++){
			block = (*pbke->free_slc_blocks.begin()).second;
			block->changeFlashMode(false);

			erase_count = consider_dynamic_wl ? block->Erase_count : 0;
			pbke->Free_block_pool.insert(std::pair<unsigned int, Block_Pool_Slot_Type*>(erase_count,block));
			pbke->free_slc_blocks.erase(pbke->free_slc_blocks.begin());
			
			//slc pool에서 제거
			iter = pbke->slc_blocks.find(block->BlockID);
			if(iter != pbke->slc_blocks.end())
				pbke->slc_blocks.erase(iter);
			else
				PRINT_ERROR("The block is not in SLC pool")
		}

		return ExecutionStatus::SUCCESS;
	}

	//SLC 영역이 아닌 경우에 대해서 min, max 계산으로 변경
	unsigned int Flash_Block_Manager_Base::Get_min_max_erase_difference(const NVM::FlashMemory::Physical_Page_Address& plane_address)
	{
		unsigned int min_erased_block = 0;
		unsigned int max_erased_block = 0;
		PlaneBookKeepingType *plane_record = &plane_manager[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID];

		for (unsigned int i = 1; i < block_no_per_plane; i++) {
			if ((plane_record->Blocks[i].Erase_count > plane_record->Blocks[max_erased_block].Erase_count) && !plane_record->Blocks[i].isSLC) {
				max_erased_block = i;
			}
			if ((plane_record->Blocks[i].Erase_count < plane_record->Blocks[min_erased_block].Erase_count) && !plane_record->Blocks[i].isSLC) {
				min_erased_block = i;
			}
		}

		return max_erased_block - min_erased_block;
	}

	//수정 계획 - SLC 블록을 따로 반환하도록 수정
	//사실상 GC_and_WL_Unit_Base::run_static_wearleveling()에 쓰이는 부분만 수정하면 됨
	//wear-leveling을 사용하지 않으면 안고쳐도 됨
	flash_block_ID_type Flash_Block_Manager_Base::Get_coldest_block_id(const NVM::FlashMemory::Physical_Page_Address& plane_address, bool is_slc)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID];

		if(is_slc)
		{
			std::map<flash_block_ID_type,Block_Pool_Slot_Type*>::iterator iter = plane_record->slc_blocks.begin();
			Block_Pool_Slot_Type *p_min_erased_block = iter->second;
			std::advance(iter,1);
			for(;iter!=plane_record->slc_blocks.end();iter++){
				if(iter->second->Erase_count < p_min_erased_block->Erase_count)
					p_min_erased_block = iter->second;
			}

			return p_min_erased_block->BlockID;
		}
		else {
			unsigned int min_erased_block = 0;
			for (unsigned int i = 1; i < block_no_per_plane; i++) {
				if(!plane_record->isBlockInSLCPool(i)) {
					if (plane_record->Blocks[i].Erase_count < plane_record->Blocks[min_erased_block].Erase_count) {
						min_erased_block = i;
					}
				}	
			}
			return min_erased_block;
		}	
	}

	PlaneBookKeepingType* Flash_Block_Manager_Base::Get_plane_bookkeeping_entry(const NVM::FlashMemory::Physical_Page_Address& plane_address)
	{
		return &(plane_manager[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID]);
	}

	//return plane_record->Blocks[block_address.BlockID].Has_ongoing_gc_wl;
	bool Flash_Block_Manager_Base::Block_has_ongoing_gc_wl(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID];
		return plane_record->Blocks[block_address.BlockID].Has_ongoing_gc_wl;
	}
	
	//해당 블록의 진행 중인 유저 프로그램 명령과 리드 명령이 없으면 true
	bool Flash_Block_Manager_Base::Can_execute_gc_wl(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID];
		return (plane_record->Blocks[block_address.BlockID].Ongoing_user_program_count + plane_record->Blocks[block_address.BlockID].Ongoing_user_read_count == 0);
	}
	
	//plane_record->Blocks[block_address.BlockID].Has_ongoing_gc_wl = true;
	void Flash_Block_Manager_Base::GC_WL_started(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID];
		plane_record->Blocks[block_address.BlockID].Has_ongoing_gc_wl = true;
	}
	void Flash_Block_Manager_Base::Cancel_GC_WL_started(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID];
		plane_record->Blocks[block_address.BlockID].Has_ongoing_gc_wl = false;
	}
	
	//plane_record->Blocks[page_address.BlockID].Ongoing_user_program_count++ 수행
	void Flash_Block_Manager_Base::program_transaction_issued(const NVM::FlashMemory::Physical_Page_Address& page_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[page_address.ChannelID][page_address.ChipID][page_address.DieID][page_address.PlaneID];
		plane_record->Blocks[page_address.BlockID].Ongoing_user_program_count++;
	}
	
	void Flash_Block_Manager_Base::Read_transaction_issued(const NVM::FlashMemory::Physical_Page_Address& page_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[page_address.ChannelID][page_address.ChipID][page_address.DieID][page_address.PlaneID];
		plane_record->Blocks[page_address.BlockID].Ongoing_user_read_count++;
	}

	void Flash_Block_Manager_Base::Program_transaction_serviced(const NVM::FlashMemory::Physical_Page_Address& page_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[page_address.ChannelID][page_address.ChipID][page_address.DieID][page_address.PlaneID];
		plane_record->Blocks[page_address.BlockID].Ongoing_user_program_count--;
	}

	void Flash_Block_Manager_Base::Read_transaction_serviced(const NVM::FlashMemory::Physical_Page_Address& page_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[page_address.ChannelID][page_address.ChipID][page_address.DieID][page_address.PlaneID];
		plane_record->Blocks[page_address.BlockID].Ongoing_user_read_count--;
	}
	
	bool Flash_Block_Manager_Base::Is_having_ongoing_program(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID];
		return plane_record->Blocks[block_address.BlockID].Ongoing_user_program_count > 0;
	}

	void Flash_Block_Manager_Base::GC_WL_finished(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID];
		plane_record->Blocks[block_address.BlockID].Has_ongoing_gc_wl = false;
	}
	
	bool Flash_Block_Manager_Base::Is_page_valid(Block_Pool_Slot_Type* block, flash_page_ID_type page_id)
	{
		if ((block->Invalid_page_bitmap[page_id / 64] & (((uint64_t)1) << page_id)) == 0) {
			return true;
		}
		return false;
	}
}
