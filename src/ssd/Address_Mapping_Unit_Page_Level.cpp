#include <cmath>
#include <assert.h>
#include <stdexcept>

#include "Address_Mapping_Unit_Page_Level.h"
#include "Stats.h"
#include "../utils/Logical_Address_Partitioning_Unit.h"

namespace SSD_Components
{
	Cached_Mapping_Table::Cached_Mapping_Table(unsigned int capacity) : capacity(capacity)
	{
	}

	Cached_Mapping_Table::~Cached_Mapping_Table()
	{
		auto entry = addressMap.begin();
		while (entry != addressMap.end()) {
			delete (*entry).second;
			addressMap.erase(entry++);
		}
	}

	inline bool Cached_Mapping_Table::Exists(const stream_id_type streamID, const LPA_type lpa)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(streamID, lpa);
		auto it = addressMap.find(key);
		if (it == addressMap.end()) {
			DEBUG("Address mapping table query - Stream ID:" << streamID << ", LPA:" << lpa << ", MISS")
				return false;
		}
		//status: free, waiting, valid
		if (it->second->Status != CMTEntryStatus::VALID) {
			DEBUG("Address mapping table query - Stream ID:" << streamID << ", LPA:" << lpa << ", MISS")
			return false;
		}

		DEBUG("Address mapping table query - Stream ID:" << streamID << ", LPA:" << lpa << ", HIT")
		return true;
	}

	PPA_type Cached_Mapping_Table::Retrieve_ppa(const stream_id_type streamID, const LPA_type lpn)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(streamID, lpn);
		auto it = addressMap.find(key);
		assert(it != addressMap.end());
		assert(it->second->Status == CMTEntryStatus::VALID);
		lruList.splice(lruList.begin(), lruList, it->second->listPtr); //다음 노드를 헤드로 이동
		
		return it->second->PPA;
	}

	page_status_type Cached_Mapping_Table::Get_bitmap_vector_of_written_sectors(const stream_id_type streamID, const LPA_type lpn)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(streamID, lpn);
		auto it = addressMap.find(key);
		assert(it != addressMap.end());
		assert(it->second->Status == CMTEntryStatus::VALID);

		return it->second->WrittenStateBitmap;
	}

	void Cached_Mapping_Table::Update_mapping_info(const stream_id_type streamID, const LPA_type lpa, const PPA_type ppa, const page_status_type pageWriteState)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(streamID, lpa);
		auto it = addressMap.find(key);
		assert(it != addressMap.end());
		assert(it->second->Status == CMTEntryStatus::VALID);
		it->second->PPA = ppa;
		it->second->WrittenStateBitmap = pageWriteState;
		it->second->Dirty = true;
		it->second->Stream_id = streamID;
		DEBUG("Address mapping table update entry - Stream ID:" << streamID << ", LPA:" << lpa << ", PPA:" << ppa)
	}

	void Cached_Mapping_Table::Insert_new_mapping_info(const stream_id_type streamID, const LPA_type lpa, const PPA_type ppa, const unsigned long long pageWriteState)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(streamID, lpa);
		auto it = addressMap.find(key);
		if (it == addressMap.end()) {
			throw std::logic_error("No slot is reserved!");
		}

		it->second->Status = CMTEntryStatus::VALID;
		it->second->PPA = ppa;
		it->second->WrittenStateBitmap = pageWriteState;
		it->second->Dirty = false;
		it->second->Stream_id = streamID;
		DEBUG("Address mapping table insert entry - Stream ID:" << streamID << ", LPA:" << lpa << ", PPA:" << ppa)
	}
	bool Cached_Mapping_Table::Is_slot_reserved_for_lpn_and_waiting(const stream_id_type streamID, const LPA_type lpn)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(streamID, lpn);
		auto it = addressMap.find(key);
		if (it != addressMap.end()) {
			if (it->second->Status == CMTEntryStatus::WAITING) {
				return true;
			}
		}

		return false;
	}

	inline bool Cached_Mapping_Table::Check_free_slot_availability()
	{
		return addressMap.size() < capacity;
	}
	
	void Cached_Mapping_Table::Reserve_slot_for_lpn(const stream_id_type streamID, const LPA_type lpn)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(streamID, lpn);

		//해당 LPN에 대한 슬랏이 존재
		if (addressMap.find(key) != addressMap.end()) {
			throw std::logic_error("Duplicate lpa insertion into CMT!");
		}
		if (addressMap.size() >= capacity) {
			throw std::logic_error("CMT overfull!");
		}

		CMTSlotType* cmtEnt = new CMTSlotType();
		cmtEnt->Dirty = false;
		cmtEnt->Stream_id = streamID;
		lruList.push_front(std::pair<LPA_type, CMTSlotType*>(key, cmtEnt));
		cmtEnt->Status = CMTEntryStatus::WAITING;
		cmtEnt->listPtr = lruList.begin();
		addressMap[key] = cmtEnt; //슬랏 생성
	}

	CMTSlotType Cached_Mapping_Table::Evict_one_slot(LPA_type& lpa)
	{
		assert(addressMap.size() > 0);
		addressMap.erase(lruList.back().first);
		lpa = UNIQUE_KEY_TO_LPN(lruList.back().second->Stream_id,lruList.back().first);
		CMTSlotType evictedItem = *lruList.back().second;
		delete lruList.back().second;
		lruList.pop_back();
	
		return evictedItem;
	}

	bool Cached_Mapping_Table::Is_dirty(const stream_id_type streamID, const LPA_type lpa)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(streamID, lpa);
		auto it = addressMap.find(key);
		if (it == addressMap.end())
		{
			throw std::logic_error("The requested slot does not exist!");
		}

		return it->second->Dirty;
	}

	void Cached_Mapping_Table::Make_clean(const stream_id_type streamID, const LPA_type lpn)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(streamID, lpn);
		auto it = addressMap.find(key);
		if (it == addressMap.end()) {
			throw std::logic_error("The requested slot does not exist!");
		}

		it->second->Dirty = false;
	}


	AddressMappingDomain::AddressMappingDomain(unsigned int cmt_capacity, unsigned int cmt_entry_size, unsigned int no_of_translation_entries_per_page,
		Cached_Mapping_Table* CMT,
		Flash_Plane_Allocation_Scheme_Type PlaneAllocationScheme,
		flash_channel_ID_type* channel_ids, unsigned int channel_no, flash_chip_ID_type* chip_ids, unsigned int chip_no,
		flash_die_ID_type* die_ids, unsigned int die_no, flash_plane_ID_type* plane_ids, unsigned int plane_no,
		PPA_type total_physical_sectors_no, LHA_type total_logical_sectors_no, unsigned int sectors_no_per_page) :
		CMT_entry_size(cmt_entry_size), Translation_entries_per_page(no_of_translation_entries_per_page), No_of_inserted_entries_in_preconditioning(0),
		PlaneAllocationScheme(PlaneAllocationScheme), Channel_no(channel_no), Chip_no(chip_no), Die_no(die_no), Plane_no(plane_no)
	{
		Total_physical_pages_no = total_physical_sectors_no / sectors_no_per_page;
		max_logical_sector_address = total_logical_sectors_no;
		Total_logical_pages_no = (max_logical_sector_address / sectors_no_per_page) + (max_logical_sector_address % sectors_no_per_page == 0? 0 : 1);
		
		Channel_ids = new flash_channel_ID_type[channel_no];
		for (flash_channel_ID_type cid = 0; cid < channel_no; cid++) {
			Channel_ids[cid] = channel_ids[cid];
		}
		
		Chip_ids = new flash_chip_ID_type[chip_no];
		for (flash_chip_ID_type cid = 0; cid < chip_no; cid++) {
			Chip_ids[cid] = chip_ids[cid];
		}
		
		Die_ids = new flash_die_ID_type[die_no];
		for (flash_die_ID_type did = 0; did < die_no; did++) {
			Die_ids[did] = die_ids[did];
		}
		
		Plane_ids = new flash_plane_ID_type[plane_no];
		for (flash_plane_ID_type pid = 0; pid < plane_no; pid++) {
			Plane_ids[pid] = plane_ids[pid];
		}

		GlobalMappingTable = new GMTEntryType[Total_logical_pages_no];
		for (unsigned int i = 0; i < Total_logical_pages_no; i++) {
			GlobalMappingTable[i].PPA = NO_PPA;
			GlobalMappingTable[i].WrittenStateBitmap = UNWRITTEN_LOGICAL_PAGE;
			GlobalMappingTable[i].TimeStamp = 0;
		}

		//If CMT is NULL, then each address mapping domain should create its own CMT
		if (CMT == NULL) {
			//Each flow (address mapping domain) has its own CMT, so CMT is create here in the constructor
			this->CMT = new Cached_Mapping_Table(cmt_capacity);
		} else {
			//The entire CMT space is shared among concurrently running flows (i.e., address mapping domains of all flow)
			this->CMT = CMT;
		}

		Total_translation_pages_no = MVPN_type(Total_logical_pages_no / Translation_entries_per_page);
		GlobalTranslationDirectory = new GTDEntryType[Total_translation_pages_no + 1];
		for (MVPN_type i = 0; i <= Total_translation_pages_no; i++) {
			GlobalTranslationDirectory[i].MPPN = (MPPN_type)NO_MPPN;
			GlobalTranslationDirectory[i].TimeStamp = INVALID_TIME_STAMP;
		}
	}

	AddressMappingDomain::~AddressMappingDomain()
	{
		delete CMT;
		delete[] GlobalMappingTable;
		delete[] GlobalTranslationDirectory;

		auto read_entry = Waiting_unmapped_read_transactions.begin();
		while (read_entry != Waiting_unmapped_read_transactions.end()) {
			delete read_entry->second;
			Waiting_unmapped_read_transactions.erase(read_entry++);
		}

		auto entry_write = Waiting_unmapped_program_transactions.begin();
		while (entry_write != Waiting_unmapped_program_transactions.end()) {
			delete entry_write->second;
			Waiting_unmapped_program_transactions.erase(entry_write++);
		}

		delete[] Channel_ids;
		delete[] Chip_ids;
		delete[] Die_ids;
		delete[] Plane_ids;
	}

	inline void AddressMappingDomain::Update_mapping_info(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa, const PPA_type ppa, const page_status_type page_status_bitmap)
	{
		if (ideal_mapping) {
			GlobalMappingTable[lpa].PPA = ppa;
			GlobalMappingTable[lpa].WrittenStateBitmap = page_status_bitmap;
			GlobalMappingTable[lpa].TimeStamp = CurrentTimeStamp;
		} else {
			CMT->Update_mapping_info(stream_id, lpa, ppa, page_status_bitmap);
		}
	}

	//return GlobalMappingTable[lpa].WrittenStateBitmap;
	inline page_status_type AddressMappingDomain::Get_page_status(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa)
	{
		if (ideal_mapping) {
			return GlobalMappingTable[lpa].WrittenStateBitmap;
		} else {
			return CMT->Get_bitmap_vector_of_written_sectors(stream_id, lpa);
		}
	}

	//ideal_mapping의 경우 GlobalMappingTable[lpa].PPA 반환
	inline PPA_type AddressMappingDomain::Get_ppa(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa)
	{
		if (ideal_mapping) {
			return GlobalMappingTable[lpa].PPA;
		} else {
			return CMT->Retrieve_ppa(stream_id, lpa);
		}
	}

	inline PPA_type AddressMappingDomain::Get_ppa_for_preconditioning(const stream_id_type stream_id, const LPA_type lpa)
	{
		return GlobalMappingTable[lpa].PPA;
	}

	//ideal mapping이면 true 반환
	inline bool AddressMappingDomain::Mapping_entry_accessible(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa)
	{
		if (ideal_mapping) {
			return true;
		} else {
			return CMT->Exists(stream_id, lpa);
		}
	}

	Address_Mapping_Unit_Page_Level* Address_Mapping_Unit_Page_Level::_my_instance = NULL;
	Address_Mapping_Unit_Page_Level::Address_Mapping_Unit_Page_Level(const sim_object_id_type& id, FTL* ftl, NVM_PHY_ONFI* flash_controller, Flash_Block_Manager_Base* block_manager,
		bool ideal_mapping_table, unsigned int cmt_capacity_in_byte, Flash_Plane_Allocation_Scheme_Type PlaneAllocationScheme,
		unsigned int concurrent_stream_no,
		unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
		std::vector<std::vector<flash_channel_ID_type>> stream_channel_ids, std::vector<std::vector<flash_chip_ID_type>> stream_chip_ids,
		std::vector<std::vector<flash_die_ID_type>> stream_die_ids, std::vector<std::vector<flash_plane_ID_type>> stream_plane_ids,
		unsigned int Block_no_per_plane, unsigned int Page_no_per_block, unsigned int SectorsPerPage, unsigned int PageSizeInByte,
		double Overprovisioning_ratio, SLC_Table *p_slc_table, CMT_Sharing_Mode sharing_mode, bool fold_large_addresses)
		: Address_Mapping_Unit_Base(id, ftl, flash_controller, block_manager, ideal_mapping_table,
			concurrent_stream_no, channel_count, chip_no_per_channel, die_no_per_chip, plane_no_per_die,
			Block_no_per_plane, Page_no_per_block, SectorsPerPage, PageSizeInByte, Overprovisioning_ratio, sharing_mode, fold_large_addresses),
			slc_table(p_slc_table)
	{
		_my_instance = this;
		domains = new AddressMappingDomain*[no_of_input_streams];

		Write_transactions_for_overfull_planes = new std::set<NVM_Transaction_Flash_WR*>***[channel_count];
		for (unsigned int channel_id = 0; channel_id < channel_count; channel_id++) {
			Write_transactions_for_overfull_planes[channel_id] = new std::set<NVM_Transaction_Flash_WR*>**[chip_no_per_channel];
			for (unsigned int chip_id = 0; chip_id < chip_no_per_channel; chip_id++) {
				Write_transactions_for_overfull_planes[channel_id][chip_id] = new std::set<NVM_Transaction_Flash_WR*>*[die_no_per_chip];
				for (unsigned int die_id = 0; die_id < die_no_per_chip; die_id++) {
					Write_transactions_for_overfull_planes[channel_id][chip_id][die_id] = new std::set<NVM_Transaction_Flash_WR*>[plane_no_per_die];
				}
			}
		}

		write_trx_for_overfull_plane = Write_transactions_for_overfull_planes;

		flash_channel_ID_type* channel_ids = NULL;
		flash_channel_ID_type* chip_ids = NULL;
		flash_channel_ID_type* die_ids = NULL;
		flash_channel_ID_type* plane_ids = NULL;
		for (unsigned int domainID = 0; domainID < no_of_input_streams; domainID++) {
			/* Since we want to have the same mapping table entry size for all streams, the entry size
			*  is calculated at this level and then pass it to the constructors of mapping domains
			* entry size = sizeOf(lpa) + sizeOf(ppn) + sizeOf(bit vector that shows written sectors of a page)
			*/
			CMT_entry_size = (unsigned int)std::ceil(((2 * std::log2(total_physical_pages_no)) + sector_no_per_page) / 8);
			//In GTD we do not need to store lpa
			GTD_entry_size = (unsigned int)std::ceil((std::log2(total_physical_pages_no) + sector_no_per_page) / 8);
			no_of_translation_entries_per_page = (SectorsPerPage * SECTOR_SIZE_IN_BYTE) / GTD_entry_size;

			Cached_Mapping_Table* sharedCMT = NULL;
			unsigned int per_stream_cmt_capacity = 0;
			cmt_capacity = cmt_capacity_in_byte / CMT_entry_size;
			switch (sharing_mode) {
				case CMT_Sharing_Mode::SHARED:
					per_stream_cmt_capacity = cmt_capacity;
					sharedCMT = new Cached_Mapping_Table(cmt_capacity);
					break;
				case CMT_Sharing_Mode::EQUAL_SIZE_PARTITIONING:
					per_stream_cmt_capacity = cmt_capacity / no_of_input_streams;
					break;
			}


			channel_ids = new flash_channel_ID_type[stream_channel_ids[domainID].size()];
			for (unsigned int i = 0; i < stream_channel_ids[domainID].size(); i++) {
				if (stream_channel_ids[domainID][i] < channel_count) {
					channel_ids[i] = stream_channel_ids[domainID][i];
				} else {
					PRINT_ERROR("Invalid channel ID specified for I/O flow " << domainID);
				}
			}

			chip_ids = new flash_channel_ID_type[stream_chip_ids[domainID].size()];
			for (unsigned int i = 0; i < stream_chip_ids[domainID].size(); i++) {
				if (stream_chip_ids[domainID][i] < chip_no_per_channel) {
					chip_ids[i] = stream_chip_ids[domainID][i];
				} else {
					PRINT_ERROR("Invalid chip ID specified for I/O flow " << domainID);
				}
			}

			die_ids = new flash_channel_ID_type[stream_die_ids[domainID].size()];
			for (unsigned int i = 0; i < stream_die_ids[domainID].size(); i++) {
				if (stream_die_ids[domainID][i] < die_no_per_chip) {
					die_ids[i] = stream_die_ids[domainID][i];
				} else {
					PRINT_ERROR("Invalid die ID specified for I/O flow " << domainID);
				}
			}

			plane_ids = new flash_channel_ID_type[stream_plane_ids[domainID].size()];
			for (unsigned int i = 0; i < stream_plane_ids[domainID].size(); i++) {
				if (stream_plane_ids[domainID][i] < plane_no_per_die) {
					plane_ids[i] = stream_plane_ids[domainID][i];
				} else {
					PRINT_ERROR("Invalid plane ID specified for I/O flow " << domainID);
				}
			}

			domains[domainID] = new AddressMappingDomain(per_stream_cmt_capacity, CMT_entry_size, no_of_translation_entries_per_page,
				sharedCMT,
				PlaneAllocationScheme,
				channel_ids, (unsigned int)(stream_channel_ids[domainID].size()), chip_ids, (unsigned int)(stream_chip_ids[domainID].size()), die_ids, 
				(unsigned int)(stream_die_ids[domainID].size()), plane_ids, (unsigned int)(stream_plane_ids[domainID].size()),
				Utils::Logical_Address_Partitioning_Unit::PDA_count_allocate_to_flow(domainID), Utils::Logical_Address_Partitioning_Unit::LHA_count_allocate_to_flow_from_device_view(domainID),
				sector_no_per_page);
			delete[] channel_ids;
			delete[] chip_ids;
			delete[] die_ids;
			delete[] plane_ids;
		}
	}

	Address_Mapping_Unit_Page_Level::~Address_Mapping_Unit_Page_Level()
	{
		for (unsigned int i = 0; i < no_of_input_streams; i++) {
			delete domains[i];
		}
		delete[] domains;
	}

	void Address_Mapping_Unit_Page_Level::Setup_triggers()
	{
		Sim_Object::Setup_triggers();
		flash_controller->ConnectToTransactionServicedSignal(handle_transaction_serviced_signal_from_PHY);
	}

	void Address_Mapping_Unit_Page_Level::Start_simulation()
	{
		Store_mapping_table_on_flash_at_start();
	}

	void Address_Mapping_Unit_Page_Level::Validate_simulation_config()
	{
	}

	void Address_Mapping_Unit_Page_Level::Execute_simulator_event(MQSimEngine::Sim_Event* event)
	{
	}

	void Address_Mapping_Unit_Page_Level::Store_mapping_table_on_flash_at_start()
	{
		if (mapping_table_stored_on_flash) {
			return;
		}
		//Since address translation functions work on flash transactions
		NVM_Transaction_Flash_WR* dummy_tr = new NVM_Transaction_Flash_WR(Transaction_Source_Type::MAPPING, 0, 0,
			NO_LPA, 0, NULL, 0, NULL, 0, 0);

		for (unsigned int stream_id = 0; stream_id < no_of_input_streams; stream_id++) {
			dummy_tr->Stream_id = stream_id;
			for (MVPN_type translation_page_id = 0; translation_page_id < domains[stream_id]->Total_translation_pages_no; translation_page_id++) {
				dummy_tr->LPA = (LPA_type)translation_page_id;
				allocate_plane_for_translation_write(dummy_tr);
				allocate_page_in_plane_for_translation_write(dummy_tr, (MVPN_type)dummy_tr->LPA, false);
				flash_controller->Change_flash_page_status_for_preconditioning(dummy_tr->Address, dummy_tr->LPA);
			}
		}
		mapping_table_stored_on_flash = true;
	}

	int Address_Mapping_Unit_Page_Level::Bring_to_CMT_for_preconditioning(stream_id_type stream_id, LPA_type lpa)
	{
		if (domains[stream_id]->GlobalMappingTable[lpa].PPA == NO_PPA) {
			PRINT_ERROR("Touching an unallocated logical address in preconditioning!")
		}

		if (domains[stream_id]->CMT->Exists(stream_id, lpa)) {
			return domains[stream_id]->No_of_inserted_entries_in_preconditioning;
		}

		if (domains[stream_id]->CMT->Check_free_slot_availability()) {
			domains[stream_id]->CMT->Reserve_slot_for_lpn(stream_id, lpa);
			domains[stream_id]->CMT->Insert_new_mapping_info(stream_id, lpa,
				domains[stream_id]->GlobalMappingTable[lpa].PPA, domains[stream_id]->GlobalMappingTable[lpa].WrittenStateBitmap);
		} else {
			LPA_type evicted_lpa;
			domains[stream_id]->CMT->Evict_one_slot(evicted_lpa);
			domains[stream_id]->CMT->Reserve_slot_for_lpn(stream_id, lpa);
			domains[stream_id]->CMT->Insert_new_mapping_info(stream_id, lpa,
				domains[stream_id]->GlobalMappingTable[lpa].PPA, domains[stream_id]->GlobalMappingTable[lpa].WrittenStateBitmap);
		}
		domains[stream_id]->No_of_inserted_entries_in_preconditioning++;
		
		return domains[stream_id]->No_of_inserted_entries_in_preconditioning;
	}

	unsigned int Address_Mapping_Unit_Page_Level::Get_cmt_capacity()
	{
		return cmt_capacity;
	}

	unsigned int Address_Mapping_Unit_Page_Level::Get_current_cmt_occupancy_for_stream(stream_id_type stream_id)
	{
		return domains[stream_id]->No_of_inserted_entries_in_preconditioning;
	}

	void Address_Mapping_Unit_Page_Level::insertHotDataLRU(LPA_type lpa, AdjustType type)
	{
		if(type==AdjustType::INACTIVE) { //Hotdata LRU에 존재하지 않는 상황
			hot_data_lru_inactive.push_front(lpa);
			auto iter = hot_data_lru_inactive.begin();
			inactive_map.insert({lpa,iter});

			if(hot_data_lru_inactive.size()>lru_size_limit) {
				LPA_type last_lpa = hot_data_lru_inactive.back();
				inactive_map.erase(last_lpa);
				hot_data_lru_inactive.pop_back();
			}
		}
		else { //AdjustType::ACTIVE, Hotdata LRU에 존재하는 상황 + Hotdata LRU에 존재하지 않을 수도 있음
			auto iter1 = active_map.find(lpa);
			auto iter2 = inactive_map.find(lpa);

			if(iter1!=active_map.end()){ //active lru에 존재하던 상황
				hot_data_lru_active.erase(iter1->second);
				hot_data_lru_active.push_front(lpa);
				
				active_map.erase(lpa);
				active_map.insert({lpa,hot_data_lru_active.begin()});
				return;
			} else if(iter2!=inactive_map.end()) { //inactive lru에 존재하는 상황
				hot_data_lru_inactive.erase(iter2->second);
				inactive_map.erase(lpa);

				hot_data_lru_active.push_front(lpa);
				auto it = hot_data_lru_active.begin();
				active_map.insert({lpa,it});
			} else { //SLC로 처리해야 하는데 용량이 다 차서 TLC로 간 경우
				hot_data_lru_active.push_front(lpa);
				auto it = hot_data_lru_active.begin();
				active_map.insert({lpa,it});
			}

			//위의 else if와 else의 공통사항으로 active lru에서 evict 처리
			if(hot_data_lru_active.size() > lru_size_limit) {
				LPA_type last_lpa = hot_data_lru_active.back();
				active_map.erase(last_lpa);
				hot_data_lru_active.pop_back();

				insertHotDataLRU(last_lpa,AdjustType::INACTIVE);
			}
		}
	}

	//Data Cache Manager => AMU => TSU (트랜잭션 리스트의 이동경로)
	//SLC Buffering은 segment reuquest에서 처리. SLC 데이터의 처리는 여기서 처리
	void Address_Mapping_Unit_Page_Level::Translate_lpa_to_ppa_and_dispatch(const std::list<NVM_Transaction*>& transactionList)
	{
		//LPA_type prev_LPA = NO_LPA;
		//translate lpa to ppa
		auto stream_id = (*transactionList.begin())->Stream_id;
		LPA_type lpa;
		PPA_type ppa;
		NVM::FlashMemory::Physical_Page_Address addr;
		PlaneBookKeepingType *pbke = NULL;
		for (std::list<NVM_Transaction*>::const_iterator it = transactionList.begin();
			it != transactionList.end(); ) {
			/*
			//hotdata lru 업데이트
			if(prev_LPA != ((NVM_Transaction_Flash*)(*it))->LPA) {
				prev_LPA = ((NVM_Transaction_Flash*)(*it))->LPA;
				adjustHotDataLRU((NVM_Transaction_Flash*)(*it));
			} 
			*/
			lpa = ((NVM_Transaction_Flash*)(*it))->LPA;
			ppa = domains[stream_id]->Get_ppa(ideal_mapping_table, stream_id, lpa);
			
		
			//NO_PPA의 경우 => SLC 버퍼링의 경우만 SLC trx 처리 & hotdata LRU 반영 필요 x
			if(ppa != NO_PPA){
				Convert_ppa_to_address(ppa,addr);
				pbke = block_manager->Get_plane_bookkeeping_entry(addr);

				//기존에 SLC에 존재하던 데이터이므로 hotdata LRU에는 존재하지 않음
				if(pbke->Blocks[addr.BlockID].isSLC){
					if(pbke->Data_wf_slc) {
						((NVM_Transaction_Flash*)(*it))->isSLCTrx = true;
					}
					else { //SLC 영역에 여유공간이 없는 경우, 버퍼링도 불가능하므로 다 TLC trx
						((NVM_Transaction_Flash*)(*it))->isSLCTrx = false;
						insertHotDataLRU(lpa,AdjustType::ACTIVE);  //insert lpa into the head of active lru
					}
				}
				else { //TLC에 존재하던 데이터
					if(inactive_map.find(lpa)!=NULL || active_map.find(lpa)!=NULL){
						insertHotDataLRU(lpa,AdjustType::ACTIVE); //insert lpa into the head of the active lru
					}
					else {
						insertHotDataLRU(lpa,AdjustType::INACTIVE);//insert lpa into the ehad of the inactive lru list
					}
				}
			}

			if (is_lpa_locked_for_gc((*it)->Stream_id, ((NVM_Transaction_Flash*)(*it))->LPA)) {
				//iterator should be post-incremented since the iterator may be deleted from list
				manage_user_transaction_facing_barrier((NVM_Transaction_Flash*)*(it++));
			} else {
				query_cmt((NVM_Transaction_Flash*)(*it++)); //transaction별 개별 피지컬 페이지 주소 할당
			}
		}

		//dispatch
		if (transactionList.size() > 0) {
			ftl->TSU->Prepare_for_transaction_submit();
			for (std::list<NVM_Transaction*>::const_iterator it = transactionList.begin();
				it != transactionList.end(); it++) {			
				if (((NVM_Transaction_Flash*)(*it))->Physical_address_determined) {					
					ftl->TSU->Submit_transaction(static_cast<NVM_Transaction_Flash*>(*it)); //NVM_Transaction_Flash에는 Physical Address 멤버변수가 존재
					if (((NVM_Transaction_Flash*)(*it))->Type == Transaction_Type::WRITE) {
						if (((NVM_Transaction_Flash_WR*)(*it))->RelatedRead != NULL) {
							ftl->TSU->Submit_transaction(((NVM_Transaction_Flash_WR*)(*it))->RelatedRead);
						}
					}
				}
			}
			
			ftl->TSU->Schedule();
		}
	}

	bool Address_Mapping_Unit_Page_Level::query_cmt(NVM_Transaction_Flash* transaction)
	{
		stream_id_type stream_id = transaction->Stream_id;
		Stats::total_CMT_queries++;
		Stats::total_CMT_queries_per_stream[stream_id]++;

		//ideal mapping의 경우
		if (domains[stream_id]->Mapping_entry_accessible(ideal_mapping_table, stream_id, transaction->LPA))//Either limited or unlimited CMT
		{
			Stats::CMT_hits_per_stream[stream_id]++;
			Stats::CMT_hits++;
			if (transaction->Type == Transaction_Type::READ) {
				Stats::total_readTR_CMT_queries_per_stream[stream_id]++;
				Stats::total_readTR_CMT_queries++;
				Stats::readTR_CMT_hits_per_stream[stream_id]++;
				Stats::readTR_CMT_hits++;
			} else {
				//This is a write transaction
				Stats::total_writeTR_CMT_queries++;
				Stats::total_writeTR_CMT_queries_per_stream[stream_id]++;
				Stats::writeTR_CMT_hits++;
				Stats::writeTR_CMT_hits_per_stream[stream_id]++;
			}

			if (translate_lpa_to_ppa(stream_id, transaction)) {
				return true;
			} else { //Stop_servicing_write() => GC 수행
				mange_unsuccessful_translation(transaction);
				return false;
			}
		} else {//Limited CMT
			//Maybe we can catch mapping data from an on-the-fly write back request
			if (request_mapping_entry(stream_id, transaction->LPA)) {
				Stats::CMT_miss++;
				Stats::CMT_miss_per_stream[stream_id]++;
				if (transaction->Type == Transaction_Type::READ) {
					Stats::total_readTR_CMT_queries++;
					Stats::total_readTR_CMT_queries_per_stream[stream_id]++;
					Stats::readTR_CMT_miss++;
					Stats::readTR_CMT_miss_per_stream[stream_id]++;
				} else { //This is a write transaction
					Stats::total_writeTR_CMT_queries++;
					Stats::total_writeTR_CMT_queries_per_stream[stream_id]++;
					Stats::writeTR_CMT_miss++;
					Stats::writeTR_CMT_miss_per_stream[stream_id]++;
				}
				if (translate_lpa_to_ppa(stream_id, transaction)) {
					return true;
				} else {
					mange_unsuccessful_translation(transaction);
					return false;
				}
			} else {
				if (transaction->Type == Transaction_Type::READ) {
					Stats::total_readTR_CMT_queries++;
					Stats::total_readTR_CMT_queries_per_stream[stream_id]++;
					Stats::readTR_CMT_miss++;
					Stats::readTR_CMT_miss_per_stream[stream_id]++;
					domains[stream_id]->Waiting_unmapped_read_transactions.insert(std::pair<LPA_type, NVM_Transaction_Flash*>(transaction->LPA, transaction));
				} else {//This is a write transaction
					Stats::total_writeTR_CMT_queries++;
					Stats::total_writeTR_CMT_queries_per_stream[stream_id]++;
					Stats::writeTR_CMT_miss++;
					Stats::writeTR_CMT_miss_per_stream[stream_id]++;
					domains[stream_id]->Waiting_unmapped_program_transactions.insert(std::pair<LPA_type, NVM_Transaction_Flash*>(transaction->LPA, transaction));
				}
			}

			return false;
		}
	}

	bool Address_Mapping_Unit_Page_Level::checkFreeSLCArea(const NVM::FlashMemory::Physical_Page_Address &plane_address, stream_id_type stream_id)
	{
		PlaneBookKeepingType *pbke = block_manager->Get_plane_bookkeeping_entry(plane_address);
		Block_Pool_Slot_Type *cur_block = pbke->Data_wf_slc[stream_id];

		if(cur_block == NULL)
			return false;
		
		return true;
	}

	bool Address_Mapping_Unit_Page_Level::checkAndAllocateNewPhysicalAddress(NVM_Transaction_Flash* transaction)
	{
		NVM::FlashMemory::Physical_Page_Address tmp_plane_address = transaction->Address;
		PlaneBookKeepingType *pbke = NULL;

		unsigned int channel = tmp_plane_address.ChannelID;
		unsigned int chip = tmp_plane_address.ChipID;
		unsigned int die = tmp_plane_address.DieID;  //하와와 유짱의 변수인것이에와요~~~~~~ pull request
		unsigned int plane = tmp_plane_address.PlaneID;

		for(unsigned int chanCnt=0;chanCnt<channel_count;chanCnt++) {
			tmp_plane_address.ChannelID = channel;
			for(unsigned int chipCnt=0;chipCnt<chip_no_per_channel;chipCnt++) {
				tmp_plane_address.ChipID = chip;
				for(unsigned int dieCnt=0;dieCnt<die_no_per_chip;dieCnt++) {
					tmp_plane_address.DieID = die;
					for(unsigned int planeCnt=0;planeCnt<plane_no_per_die;planeCnt++) {
						tmp_plane_address.PlaneID = plane;
						if(checkFreeSLCArea(tmp_plane_address,transaction->Stream_id))
						{
							transaction->Address = tmp_plane_address;
							return true;
						}
						plane = (++plane) % plane_no_per_die;
					}
					die = (++die) % die_no_per_chip;
				}
				chip = (++chip) % chip_no_per_channel;
			}
			channel = (++channel) % channel_count;
		}

		return false;
	}

	/*
	//trnsaction->LPA는 stream_id 내부의 lpa를 나타내지만, 실험환경에서는 multi-stream을 사용하지 않을 거라 LPA값만을 통해서 비교
	void Address_Mapping_Unit_Page_Level::adjustHotDataLRU(NVM_Transaction_Flash* transaction)
	{
		bool exists = false;
		std::list<std::pair<bool,LPA_type>>::iterator iter;
		for(iter=hot_data_lru_active.begin();iter!=hot_data_lru_active.end();iter++){
			if((*iter).second==transaction->LPA){
				exists = true;
				break;
			}
		}
		if(exists){
			//write trx의 경우 slc buffering으로 slc영역에 쓰인다면 바로 true값을 넣어줘야 함
			if(transaction->Type==Transaction_Type::WRITE)
				hot_data_lru_active.push_front({transaction->isSLCTrx,iter->second});
			else 
				hot_data_lru_active.push_front({iter->first,iter->second});

			hot_data_lru_active.erase(iter);
		}
		else {
			for(iter=hot_data_lru_inactive.begin();iter!=hot_data_lru_inactive.end();iter++){
				if((*iter).second==transaction->LPA){
					exists = true;
					break;
				}
			}
			if(exists){ //active list tail의 데이터를 inactive list head로 이동
				if(transaction->Type==Transaction_Type::WRITE)
					hot_data_lru_active.push_front({transaction->isSLCTrx,iter->second});
				else
					hot_data_lru_active.push_front({iter->first,iter->second});
				
				hot_data_lru_inactive.erase(iter);

				if(hot_data_lru_active.size()>lru_size_limit){
					iter = (hot_data_lru_active.end())--;
					hot_data_lru_inactive.push_front((*iter));
				}
			}
			else {  //아예 존재하지 않는 경우 inactive list의 head에 삽입
				if(transaction->Type==Transaction_Type::WRITE)
					hot_data_lru_inactive.push_front({transaction->isSLCTrx,transaction->LPA});
				else //LRU에 존재하지 않던 데이터에 대하여 접근이 발생한 경우 SLC영역에 존재하는지 아닌지 아직은 정확히 판단할 수 없음 (수정 필요)
					hot_data_lru_inactive.push_front({false,transaction->LPA});
				
				if(hot_data_lru_inactive.size()>lru_size_limit) {
					iter = (hot_data_lru_inactive.end())--;
					hot_data_lru_inactive.erase(iter);
				}
			}
		}
	}*/

	/*This function should be invoked only if the address translation entry exists in CMT.
	* Otherwise, the call to the CMT->Rerieve_ppa, within this function, will throw an exception.
	*/
	bool Address_Mapping_Unit_Page_Level::translate_lpa_to_ppa(stream_id_type streamID, NVM_Transaction_Flash* transaction)
	{
		//이전에 program된 데이터의 PPA를 GlobalMapTable에서 가져옴
		PPA_type ppa = domains[streamID]->Get_ppa(ideal_mapping_table, streamID, transaction->LPA);
		if (transaction->Type == Transaction_Type::READ) {
			if (ppa == NO_PPA) {
				transaction->isSLCTrx = false;
				ppa = online_create_entry_for_reads(transaction->LPA, streamID, transaction->Address, ((NVM_Transaction_Flash_RD*)transaction)->read_sectors_bitmap);
			}
			transaction->PPA = ppa;
			Convert_ppa_to_address(transaction->PPA, transaction->Address);
			block_manager->Read_transaction_issued(transaction->Address);
			transaction->Physical_address_determined = true;
		} else {//This is a write transaction
			allocate_plane_for_user_write((NVM_Transaction_Flash_WR*)transaction); //plane 위치까지 결정

			//slc free block pool에 여유 공간이 있는지 확인, 없으면 새로운 physical address 부여, ssd 전체에 존재하지 않으면 tlc로 전환
			if(transaction->isSLCTrx) {
				if(!checkAndAllocateNewPhysicalAddress(transaction)) {
					transaction->isSLCTrx = false;
					//slc_table->changeEntryModeTo(transaction->Stream_id,transaction->LPA,Flash_Technology_Type::TLC);
					//std::cout<<"compare: "<<(slc_table->isLPAEntrySLC(transaction->Stream_id,transaction->LPA)==true)<<std::endl;
				}
			}
			
			//there are too few free pages remaining only for GC
			if (ftl->GC_and_WL_Unit->Stop_servicing_writes(transaction->Address,false)){ //slc에 여유공간이 없으면 tlc로 전환하므로 여기서는 is_slc = false로 전달
				std::cout<<"Address_Mapping_Unit_Page_Level::stop servicing writes"<<std::endl;
				return false;
			}

			//여기서 ppa 정함
			allocate_page_in_plane_for_user_write((NVM_Transaction_Flash_WR*)transaction, false);
			transaction->Physical_address_determined = true;
		}
		
		return true;
	}
	
	void Address_Mapping_Unit_Page_Level::Allocate_address_for_preconditioning(const stream_id_type stream_id, std::map<LPA_type, page_status_type>& lpa_list, std::vector<double>& steady_state_distribution)
	{
		int idx = 0;
		std::vector<LPA_type>**** assigned_lpas = new std::vector<LPA_type>***[channel_count];
		for (unsigned int channel_cntr = 0; channel_cntr < channel_count; channel_cntr++) {
			assigned_lpas[channel_cntr] = new std::vector<LPA_type>**[chip_no_per_channel];
			for (unsigned int chip_cntr = 0; chip_cntr < chip_no_per_channel; chip_cntr++) {
				assigned_lpas[channel_cntr][chip_cntr] = new std::vector<LPA_type>*[die_no_per_chip];
				for (unsigned int die_cntr = 0; die_cntr < die_no_per_chip; die_cntr++) {
					assigned_lpas[channel_cntr][chip_cntr][die_cntr] = new std::vector<LPA_type>[plane_no_per_die];
				}
			}
		}

		//First: distribute LPAs to planes
		NVM::FlashMemory::Physical_Page_Address plane_address;
		for (auto lpa = lpa_list.begin(); lpa != lpa_list.end();) {
			if ((*lpa).first >= domains[stream_id]->Total_logical_pages_no) {
				PRINT_ERROR("Out of range LPA specified for preconditioning! LPA shoud be smaller than " << domains[stream_id]->Total_logical_pages_no << ", but it is " << (*lpa).first)
			}
			PPA_type ppa = domains[stream_id]->Get_ppa_for_preconditioning(stream_id, (*lpa).first);
			if (ppa != NO_LPA) {
				PRINT_ERROR("Calling address allocation for a previously allocated LPA during preconditioning!")
			}
			allocate_plane_for_preconditioning(stream_id, (*lpa).first, plane_address);
			if (LPA_type(Utils::Logical_Address_Partitioning_Unit::Get_share_of_physcial_pages_in_plane(plane_address.ChannelID, plane_address.ChipID, plane_address.DieID, plane_address.PlaneID) * page_no_per_plane)
				> assigned_lpas[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].size()) {
				assigned_lpas[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].push_back((*lpa).first);
				lpa++;
			} else {
				lpa_list.erase(lpa++);
			}
		}

		//Second: distribute LPAs within planes based on the steady-state status of blocks
		//unsigned int safe_guard_band = ftl->GC_and_WL_Unit->Get_minimum_number_of_free_pages_before_GC();
		for (unsigned int channel_cntr = 0; channel_cntr < domains[stream_id]->Channel_no; channel_cntr++) {
			for (unsigned int chip_cntr = 0; chip_cntr < domains[stream_id]->Chip_no; chip_cntr++) {
				for (unsigned int die_cntr = 0; die_cntr < domains[stream_id]->Die_no; die_cntr++) {
					for (unsigned int plane_cntr = 0; plane_cntr < domains[stream_id]->Plane_no; plane_cntr++) {
						plane_address.ChannelID = domains[stream_id]->Channel_ids[channel_cntr];
						plane_address.ChipID = domains[stream_id]->Chip_ids[chip_cntr];
						plane_address.DieID = domains[stream_id]->Die_ids[die_cntr];
						plane_address.PlaneID = domains[stream_id]->Plane_ids[plane_cntr];

						unsigned int physical_block_consumption_goal = (unsigned int)(double(block_no_per_plane - ftl->GC_and_WL_Unit->Get_minimum_number_of_free_pages_before_GC() / 2)
							* Utils::Logical_Address_Partitioning_Unit::Get_share_of_physcial_pages_in_plane(plane_address.ChannelID, plane_address.ChipID, plane_address.DieID, plane_address.PlaneID));

						//Adjust the average
						double model_average = 0;
						std::vector<double> adjusted_steady_state_distribution;
						//Check if probability distribution is correct 
						for (unsigned int i = 0; i <= pages_no_per_block; i++) {
							model_average += steady_state_distribution[i] * double(i) / double(pages_no_per_block);
							adjusted_steady_state_distribution.push_back(steady_state_distribution[i]);
						}
						double real_average = double(assigned_lpas[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].size()) / (physical_block_consumption_goal * pages_no_per_block);
						if (std::abs(model_average - real_average) * pages_no_per_block > 0.9999) {
							int displacement_index = int((real_average - model_average) * pages_no_per_block);
							if (displacement_index > 0) {
								for (int i = 0; i < displacement_index; i++) {
									adjusted_steady_state_distribution[i] = 0;
								}
								for (int i = displacement_index; i < int(pages_no_per_block); i++) {
									adjusted_steady_state_distribution[i] = steady_state_distribution[i - displacement_index];
								}
							} else {
								displacement_index *= -1;
								for (int i = 0; i < int(pages_no_per_block) - displacement_index; i++) {
									adjusted_steady_state_distribution[i] = steady_state_distribution[i + displacement_index];
								}
								for (int i = int(pages_no_per_block) - displacement_index; i < int(pages_no_per_block); i++) {
									adjusted_steady_state_distribution[i] = 0;
								}
							}
						}

						//Check if it is possible to find a PPA for each LPA with current proability assignments 
						unsigned int total_valid_pages = 0;
						for (int valid_pages_in_block = pages_no_per_block; valid_pages_in_block >= 0; valid_pages_in_block--) {
							total_valid_pages += valid_pages_in_block * (unsigned int)(adjusted_steady_state_distribution[valid_pages_in_block] * physical_block_consumption_goal);
						}
						unsigned int pages_need_PPA = 0;//The number of LPAs that remain unassigned due to imperfect probability assignments
						if (total_valid_pages < assigned_lpas[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].size()) {
							pages_need_PPA = (unsigned int)(assigned_lpas[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].size()) - total_valid_pages;
						}
						
						unsigned int remaining_blocks_to_consume = physical_block_consumption_goal;
						for (int valid_pages_in_block = pages_no_per_block; valid_pages_in_block >= 0; valid_pages_in_block--) {
							unsigned int block_no_with_x_valid_page = (unsigned int)(adjusted_steady_state_distribution[valid_pages_in_block] * physical_block_consumption_goal);
							if (block_no_with_x_valid_page > 0 && pages_need_PPA > 0) {
								block_no_with_x_valid_page += (pages_need_PPA / valid_pages_in_block) + (pages_need_PPA % valid_pages_in_block == 0 ? 0 : 1);
								pages_need_PPA = 0;
							}

							if (block_no_with_x_valid_page <= remaining_blocks_to_consume) {
								remaining_blocks_to_consume -= block_no_with_x_valid_page;
							} else {
								block_no_with_x_valid_page = remaining_blocks_to_consume;
								remaining_blocks_to_consume = 0;
							}

							for (unsigned int block_cntr = 0; block_cntr < block_no_with_x_valid_page; block_cntr++) {
								//Assign physical addresses
								std::vector<NVM::FlashMemory::Physical_Page_Address> addresses;
								if (assigned_lpas[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].size() < valid_pages_in_block) {
									valid_pages_in_block = int(assigned_lpas[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].size());
								}
								for (int page_cntr = 0; page_cntr < valid_pages_in_block; page_cntr++) {
									NVM::FlashMemory::Physical_Page_Address addr(plane_address.ChannelID, plane_address.ChipID, plane_address.DieID, plane_address.PlaneID, 0, 0);
									addresses.push_back(addr);
								}
								block_manager->Allocate_Pages_in_block_and_invalidate_remaining_for_preconditioning(stream_id, plane_address, addresses);

								//Update mapping table
								for (auto const &address : addresses) {
									LPA_type lpa = assigned_lpas[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].back();
									assigned_lpas[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].pop_back();
									PPA_type ppa = Convert_address_to_ppa(address);
									flash_controller->Change_memory_status_preconditioning(&address, &lpa);
									domains[stream_id]->GlobalMappingTable[lpa].PPA = ppa;
									domains[stream_id]->GlobalMappingTable[lpa].WrittenStateBitmap = (*lpa_list.find(lpa)).second;
									domains[stream_id]->GlobalMappingTable[lpa].TimeStamp = 0;
								}
							}
						}
						if (assigned_lpas[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].size() > 0) {
							PRINT_ERROR("It is not possible to assign PPA to all LPAs in Allocate_address_for_preconditioning! It is not safe to continue preconditioning." << assigned_lpas[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].size())
						}
					}
				}
			}
		}

		for (unsigned int channel_cntr = 0; channel_cntr < channel_count; channel_cntr++) {
			for (unsigned int chip_cntr = 0; chip_cntr < chip_no_per_channel; chip_cntr++) {
				for (unsigned int die_cntr = 0; die_cntr < die_no_per_chip; die_cntr++) {
					delete[] assigned_lpas[channel_cntr][chip_cntr][die_cntr];
				}
				delete[] assigned_lpas[channel_cntr][chip_cntr];
			}
			delete[] assigned_lpas[channel_cntr];
		}
		delete[] assigned_lpas;
	}

	void Address_Mapping_Unit_Page_Level::Allocate_new_page_for_gc(NVM_Transaction_Flash_WR* transaction, bool is_translation_page)
	{
		if (is_translation_page) {
			MPPN_type mppn = domains[transaction->Stream_id]->GlobalTranslationDirectory[transaction->LPA].MPPN;
			if (mppn == NO_MPPN) {
				PRINT_ERROR("Unexpected situation occured for gc write in Allocate_new_page_for_gc function!")
			}

			allocate_page_in_plane_for_translation_write(transaction, (MVPN_type)transaction->LPA, true);
			transaction->Physical_address_determined = true;
		} else {
			//ideal mapping이 아닌 경우
			if (!domains[transaction->Stream_id]->Mapping_entry_accessible(ideal_mapping_table, transaction->Stream_id, transaction->LPA)) {
				if (!domains[transaction->Stream_id]->CMT->Check_free_slot_availability()) {
					LPA_type evicted_lpa;
					CMTSlotType evictedItem = domains[transaction->Stream_id]->CMT->Evict_one_slot(evicted_lpa);
					if (evictedItem.Dirty) {
						/* In order to eliminate possible race conditions for the requests that
						* will access the evicted lpa in the near future (before the translation
						* write finishes), MQSim updates GMT (the on flash mapping table) right
						* after eviction happens.*/
						domains[transaction->Stream_id]->GlobalMappingTable[evicted_lpa].PPA = evictedItem.PPA;
						domains[transaction->Stream_id]->GlobalMappingTable[evicted_lpa].WrittenStateBitmap = evictedItem.WrittenStateBitmap;
						if (domains[transaction->Stream_id]->GlobalMappingTable[evicted_lpa].TimeStamp > CurrentTimeStamp) {
							throw std::logic_error("Unexpected situation occured in handling GMT!");
						}
						domains[transaction->Stream_id]->GlobalMappingTable[evicted_lpa].TimeStamp = CurrentTimeStamp;
						generate_flash_writeback_request_for_mapping_data(transaction->Stream_id, evicted_lpa);
					}
				}
				domains[transaction->Stream_id]->CMT->Reserve_slot_for_lpn(transaction->Stream_id, transaction->LPA);
				domains[transaction->Stream_id]->CMT->Insert_new_mapping_info(transaction->Stream_id, transaction->LPA, Convert_address_to_ppa(transaction->Address), transaction->write_sectors_bitmap);
			}

			allocate_page_in_plane_for_user_write(transaction, true);
			transaction->Physical_address_determined = true;

			//the mapping entry should be updated
			stream_id_type stream_id = transaction->Stream_id;
			Stats::total_CMT_queries++;
			Stats::total_CMT_queries_per_stream[stream_id]++;

			//either limited or unlimited mapping
			if (domains[stream_id]->Mapping_entry_accessible(ideal_mapping_table, stream_id, transaction->LPA)) {
				Stats::CMT_hits++;
				Stats::CMT_hits_per_stream[stream_id]++;
				Stats::total_writeTR_CMT_queries++;
				Stats::total_writeTR_CMT_queries_per_stream[stream_id]++;
				Stats::writeTR_CMT_hits++;
				Stats::writeTR_CMT_hits_per_stream[stream_id]++;
				domains[stream_id]->Update_mapping_info(ideal_mapping_table, stream_id, transaction->LPA, transaction->PPA, transaction->write_sectors_bitmap);
			} else { //the else block only executed for non-ideal mapping table in which CMT has a limited capacity and mapping data is read/written from/to the flash storage
				if (!domains[stream_id]->CMT->Check_free_slot_availability()) {
					LPA_type evicted_lpa;
					CMTSlotType evictedItem = domains[stream_id]->CMT->Evict_one_slot(evicted_lpa);
					if (evictedItem.Dirty) {
						/* In order to eliminate possible race conditions for the requests that
						* will access the evicted lpa in the near future (before the translation
						* write finishes), MQSim updates GMT (the on flash mapping table) right
						* after eviction happens.*/
						domains[stream_id]->GlobalMappingTable[evicted_lpa].PPA = evictedItem.PPA;
						domains[stream_id]->GlobalMappingTable[evicted_lpa].WrittenStateBitmap = evictedItem.WrittenStateBitmap;
						if (domains[stream_id]->GlobalMappingTable[evicted_lpa].TimeStamp > CurrentTimeStamp)
							throw std::logic_error("Unexpected situation occured in handling GMT!");
						domains[stream_id]->GlobalMappingTable[evicted_lpa].TimeStamp = CurrentTimeStamp;
						generate_flash_writeback_request_for_mapping_data(stream_id, evicted_lpa);
					}
				}
				domains[stream_id]->CMT->Reserve_slot_for_lpn(stream_id, transaction->LPA);
				domains[stream_id]->CMT->Insert_new_mapping_info(stream_id, transaction->LPA, transaction->PPA, transaction->write_sectors_bitmap);
			}
		}
	}

	//Plane 위치까지 결정
	void Address_Mapping_Unit_Page_Level::allocate_plane_for_preconditioning(stream_id_type stream_id, LPA_type lpn, NVM::FlashMemory::Physical_Page_Address& targetAddress)
	{
		AddressMappingDomain* domain = domains[stream_id];

		switch (domain->PlaneAllocationScheme) {
			case Flash_Plane_Allocation_Scheme_Type::CWDP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CWPD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Channel_no * domain->Chip_no * domain->Plane_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Channel_no * domain->Chip_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CDWP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CDPW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CPWD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Chip_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CPDW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Plane_no)];
				break;
				//Static: Way first
			case Flash_Plane_Allocation_Scheme_Type::WCDP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Channel_no * domain->Die_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WCPD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Channel_no * domain->Plane_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WDCP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Die_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Die_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WDPC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Die_no * domain->Plane_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Die_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WPCD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Plane_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Plane_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WPDC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Plane_no * domain->Die_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Plane_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Plane_no)];
				break;
				//Static: Die first
			case Flash_Plane_Allocation_Scheme_Type::DCWP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Die_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no * domain->Chip_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DCPW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Die_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no * domain->Plane_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DWCP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Die_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DWPC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no * domain->Plane_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Die_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DPCW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Die_no * domain->Plane_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Die_no * domain->Plane_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Die_no) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DPWC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Die_no * domain->Plane_no * domain->Chip_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Die_no * domain->Plane_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Die_no) % domain->Plane_no)];
				break;
				//Static: Plane first
			case Flash_Plane_Allocation_Scheme_Type::PCWD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no * domain->Chip_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PCDW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no * domain->Die_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PWCD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Chip_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Chip_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PWDC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Chip_no * domain->Die_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Chip_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PDCW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PDWC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no * domain->Chip_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			default:
				PRINT_ERROR("Unknown plane allocation scheme type!")
		}
	}

	//Flash_Plane_Allocation_Scheme에 따라 Plane까지의 Physical Address가 정해짐
	void Address_Mapping_Unit_Page_Level::allocate_plane_for_user_write(NVM_Transaction_Flash_WR* transaction)
	{
		LPA_type lpn = transaction->LPA;
		NVM::FlashMemory::Physical_Page_Address& targetAddress = transaction->Address;
		AddressMappingDomain* domain = domains[transaction->Stream_id];

		switch (domain->PlaneAllocationScheme) {
			case Flash_Plane_Allocation_Scheme_Type::CWDP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CWPD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Channel_no * domain->Chip_no * domain->Plane_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Channel_no * domain->Chip_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CDWP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CDPW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CPWD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Chip_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CPDW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)(lpn % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Channel_no) % domain->Plane_no)];
				break;
				//Static: Way first
			case Flash_Plane_Allocation_Scheme_Type::WCDP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Channel_no * domain->Die_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WCPD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Channel_no * domain->Plane_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WDCP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Die_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Die_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WDPC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Die_no * domain->Plane_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Die_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WPCD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Plane_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Plane_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WPDC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Plane_no * domain->Die_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)(lpn % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Chip_no * domain->Plane_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Chip_no) % domain->Plane_no)];
				break;
				//Static: Die first
			case Flash_Plane_Allocation_Scheme_Type::DCWP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Die_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no * domain->Chip_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DCPW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Die_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no * domain->Plane_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DWCP:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Die_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DWPC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no * domain->Plane_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Die_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / (domain->Die_no * domain->Chip_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DPCW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Die_no * domain->Plane_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Die_no * domain->Plane_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Die_no) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DPWC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Die_no * domain->Plane_no * domain->Chip_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Die_no * domain->Plane_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)(lpn % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)((lpn / domain->Die_no) % domain->Plane_no)];
				break;
				//Static: Plane first
			case Flash_Plane_Allocation_Scheme_Type::PCWD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no * domain->Chip_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PCDW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no * domain->Die_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PWCD:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Chip_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Chip_no * domain->Channel_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PWDC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Chip_no * domain->Die_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Chip_no)) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PDCW:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PDWC:
				targetAddress.ChannelID = domain->Channel_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no * domain->Chip_no)) % domain->Channel_no)];
				targetAddress.ChipID = domain->Chip_ids[(unsigned int)((lpn / (domain->Plane_no * domain->Die_no)) % domain->Chip_no)];
				targetAddress.DieID = domain->Die_ids[(unsigned int)((lpn / domain->Plane_no) % domain->Die_no)];
				targetAddress.PlaneID = domain->Plane_ids[(unsigned int)(lpn % domain->Plane_no)];
				break;
			default:
				PRINT_ERROR("Unknown plane allocation scheme type!")
		}
	}

	//pbke에서 page수 조정하고 블록 & 페이지까지 할당 + invalidate block도 수행
	//gc를 위한 경우도 여기서 Allocate_block_and_page_in_plane_for_gc_write()를 호출함
	void Address_Mapping_Unit_Page_Level::allocate_page_in_plane_for_user_write(NVM_Transaction_Flash_WR* transaction, bool is_for_gc)
	{
		AddressMappingDomain* domain = domains[transaction->Stream_id];
		PPA_type old_ppa = domain->Get_ppa(ideal_mapping_table, transaction->Stream_id, transaction->LPA);

		if (old_ppa == NO_PPA)  /*this is the first access to the logical page*/
		{
			if (is_for_gc) {
				PRINT_ERROR("Unexpected mapping table status in allocate_page_in_plane_for_user_write function for a GC/WL write!")
			}
		} else { //처음 쓰여지는 데이터가 아닌 경우
			if (is_for_gc) {
				NVM::FlashMemory::Physical_Page_Address addr;
				Convert_ppa_to_address(old_ppa, addr);
				block_manager->Invalidate_page_in_block(transaction->Stream_id, addr);
				page_status_type page_status_in_cmt = domain->Get_page_status(ideal_mapping_table, transaction->Stream_id, transaction->LPA);
				if (page_status_in_cmt != transaction->write_sectors_bitmap)
					PRINT_ERROR("Unexpected mapping table status in allocate_page_in_plane_for_user_write for a GC/WL write!")
			} else {
				//prev page status: global mapping table에 적혀있는 페이지 상태 정보
				//status intersection: 페이지내에서 새로운 섹터에 쓰이는 것까지 반영한 페이지 내의 섹터 정보
				page_status_type prev_page_status = domain->Get_page_status(ideal_mapping_table, transaction->Stream_id, transaction->LPA);
				page_status_type status_intersection = transaction->write_sectors_bitmap & prev_page_status;
				//check if an update read is required
				if (status_intersection == prev_page_status) { //기존에 쓰인 섹터에 대한 업데이트이므로 페이지를 invalid로 표시하고 새로 써야 함
					NVM::FlashMemory::Physical_Page_Address addr;
					Convert_ppa_to_address(old_ppa, addr);
					block_manager->Invalidate_page_in_block(transaction->Stream_id, addr);
				} else {
					//안 쓰였던 섹터에 내용을 새로 쓰는 경우, 기존에 쓰인 내용을 다 읽고 새로 써야 함
					page_status_type read_pages_bitmap = status_intersection ^ prev_page_status; //read update가 필요한 sector의 비트맵
					NVM_Transaction_Flash_RD *update_read_tr = new NVM_Transaction_Flash_RD(transaction->Source, transaction->Stream_id,
						count_sector_no_from_status_bitmap(read_pages_bitmap) * SECTOR_SIZE_IN_BYTE, transaction->LPA, old_ppa, transaction->UserIORequest,
						transaction->Content, transaction, read_pages_bitmap, domain->GlobalMappingTable[transaction->LPA].TimeStamp, transaction->isSLCTrx);
					Convert_ppa_to_address(old_ppa, update_read_tr->Address);
					block_manager->Read_transaction_issued(update_read_tr->Address);//Inform block manager about a new transaction as soon as the transaction's target address is determined
					block_manager->Invalidate_page_in_block(transaction->Stream_id, update_read_tr->Address);
					transaction->RelatedRead = update_read_tr;
				}
			}
		}

		/* 23.03.29 -> 다른 모듈의 slc table의 값과 연동이 잘 안 됨 => transaction->isSLCTrx로 판단해야 함
		bool isSLC = slc_table->isLPAEntrySLC(transaction->Stream_id,transaction->LPA);
		if(isSLC != transaction->isSLCTrx)
		{
			std::cout<<"isSLC: "<<isSLC<<", transaction->isSLCTrx: "<<transaction->isSLCTrx<<std::endl; 
			PRINT_ERROR("not matching slc table entry")
		}
		*/
			
		/*The following lines should not be ordered with respect to the block_manager->Invalidate_page_in_block
		* function call in the above code blocks. Otherwise, GC may be invoked (due to the call to Allocate_block_....) and
		* may decide to move a page that is just invalidated.*/
		if (is_for_gc) {
			block_manager->Allocate_block_and_page_in_plane_for_gc_write(transaction->Stream_id, transaction->Address);
		} else {
			block_manager->Allocate_block_and_page_in_plane_for_user_write(transaction->Stream_id, transaction->Address, transaction->isSLCTrx);
		}
		transaction->PPA = Convert_address_to_ppa(transaction->Address);
		domain->Update_mapping_info(ideal_mapping_table, transaction->Stream_id, transaction->LPA, transaction->PPA,
			((NVM_Transaction_Flash_WR*)transaction)->write_sectors_bitmap | domain->Get_page_status(ideal_mapping_table, transaction->Stream_id, transaction->LPA));
	}

	void Address_Mapping_Unit_Page_Level::allocate_plane_for_translation_write(NVM_Transaction_Flash* transaction)
	{
		allocate_plane_for_user_write((NVM_Transaction_Flash_WR*)transaction);
	}

	//pagemap table을 플래시 메모리에 저장하기 위한 함수로 시뮬레이션 시작할 때에만 호출 됨
	void Address_Mapping_Unit_Page_Level::allocate_page_in_plane_for_translation_write(NVM_Transaction_Flash* transaction, MVPN_type mvpn, bool is_for_gc)
	{
		AddressMappingDomain* domain = domains[transaction->Stream_id];

		MPPN_type old_MPPN = domain->GlobalTranslationDirectory[mvpn].MPPN;
		/*this is the first access to the mvpn*/
		if (old_MPPN == NO_MPPN) {
			if (is_for_gc) {
				PRINT_ERROR("Unexpected mapping table status in allocate_page_in_plane_for_translation_write for a GC/WL write!")
			}
		} else {
			NVM::FlashMemory::Physical_Page_Address prevAddr;
			Convert_ppa_to_address(old_MPPN, prevAddr);
			block_manager->Invalidate_page_in_block(transaction->Stream_id, prevAddr);
		}

		block_manager->Allocate_block_and_page_in_plane_for_translation_write(transaction->Stream_id, transaction->Address, false);
		transaction->PPA = Convert_address_to_ppa(transaction->Address);
		domain->GlobalTranslationDirectory[mvpn].MPPN = (MPPN_type)transaction->PPA;
		domain->GlobalTranslationDirectory[mvpn].TimeStamp = CurrentTimeStamp;
	}

	//plane allocation 기법에 따라 Physical_Page_Address& read_address에 위치 정보 기록
	/** Physical_Address read_address
	 * block_manager->Allocate_block_and_page_in_plane_for_user_write(stream_id, read_address);
	 * PPA_type ppa = Convert_address_to_ppa(read_address);
	 * domain->Update_mapping_info(ideal_mapping_table, stream_id, lpa, ppa, read_sectors_bitmap);
	*/
	PPA_type Address_Mapping_Unit_Page_Level::online_create_entry_for_reads(LPA_type lpa, const stream_id_type stream_id, NVM::FlashMemory::Physical_Page_Address& read_address, uint64_t read_sectors_bitmap)
	{
		AddressMappingDomain* domain = domains[stream_id];
		switch (domain->PlaneAllocationScheme) {
			//Static: Channel first
			case Flash_Plane_Allocation_Scheme_Type::CWDP:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)(lpa % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / domain->Channel_no) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Chip_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Chip_no * domain->Die_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CWPD:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)(lpa % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / domain->Channel_no) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Chip_no * domain->Plane_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Chip_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CDWP:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)(lpa % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Die_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / domain->Channel_no) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Die_no * domain->Chip_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CDPW:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)(lpa % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Die_no * domain->Plane_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / domain->Channel_no) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Die_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CPWD:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)(lpa % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Plane_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Plane_no * domain->Chip_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / domain->Channel_no) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::CPDW:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)(lpa % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Plane_no * domain->Die_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Channel_no * domain->Plane_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / domain->Channel_no) % domain->Plane_no)];
				break;
				//Static: Way first
			case Flash_Plane_Allocation_Scheme_Type::WCDP:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / domain->Chip_no) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)(lpa % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Channel_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Channel_no * domain->Die_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WCPD:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / domain->Chip_no) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)(lpa % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Channel_no * domain->Plane_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WDCP:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Die_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)(lpa % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / domain->Chip_no) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Die_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WDPC:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Die_no * domain->Plane_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)(lpa % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / domain->Chip_no) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Die_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WPCD:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Plane_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)(lpa % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Plane_no * domain->Channel_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / domain->Chip_no) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::WPDC:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Plane_no * domain->Die_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)(lpa % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Chip_no * domain->Plane_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / domain->Chip_no) % domain->Plane_no)];
				break;
				//Static: Die first
			case Flash_Plane_Allocation_Scheme_Type::DCWP:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / domain->Die_no) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)(lpa % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / (domain->Die_no * domain->Channel_no * domain->Chip_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DCPW:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / domain->Die_no) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Die_no * domain->Channel_no * domain->Plane_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)(lpa % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / (domain->Die_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DWCP:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Die_no * domain->Chip_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / domain->Die_no) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)(lpa % domain->Die_no)];
				read_address.PlaneID = domain->Die_ids[(unsigned int)((lpa / (domain->Die_no * domain->Chip_no * domain->Channel_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DWPC:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Die_no * domain->Chip_no * domain->Plane_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / domain->Die_no) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)(lpa % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / (domain->Die_no * domain->Chip_no)) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DPCW:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Die_no * domain->Plane_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Die_no * domain->Plane_no * domain->Channel_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)(lpa % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / domain->Die_no) % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::DPWC:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Die_no * domain->Plane_no * domain->Chip_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Die_no * domain->Plane_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)(lpa % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)((lpa / domain->Die_no) % domain->Plane_no)];
				break;
				//Static: Plane first
			case Flash_Plane_Allocation_Scheme_Type::PCWD:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / domain->Plane_no) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Channel_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Channel_no * domain->Chip_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)(lpa % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PCDW:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / domain->Plane_no) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Channel_no * domain->Die_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Channel_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)(lpa % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PWCD:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Chip_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / domain->Plane_no) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Chip_no * domain->Channel_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)(lpa % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PWDC:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Chip_no * domain->Die_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / domain->Plane_no) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Chip_no)) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)(lpa % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PDCW:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Die_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Die_no * domain->Channel_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / domain->Plane_no) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)(lpa % domain->Plane_no)];
				break;
			case Flash_Plane_Allocation_Scheme_Type::PDWC:
				read_address.ChannelID = domain->Channel_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Die_no * domain->Chip_no)) % domain->Channel_no)];
				read_address.ChipID = domain->Chip_ids[(unsigned int)((lpa / (domain->Plane_no * domain->Die_no)) % domain->Chip_no)];
				read_address.DieID = domain->Die_ids[(unsigned int)((lpa / domain->Plane_no) % domain->Die_no)];
				read_address.PlaneID = domain->Plane_ids[(unsigned int)(lpa % domain->Plane_no)];
				break;
			default:
				PRINT_ERROR("Unknown plane allocation scheme type!")
		}

		//할당된 plane에 대하여 block을 지정하여 bookkeeping (Data_wf에서 프리블록을 받아와서 page까지 위치 할당 완료)
		block_manager->Allocate_block_and_page_in_plane_for_user_write(stream_id, read_address); 
		PPA_type ppa = Convert_address_to_ppa(read_address);
		domain->Update_mapping_info(ideal_mapping_table, stream_id, lpa, ppa, read_sectors_bitmap);

		return ppa;
	}

	inline void Address_Mapping_Unit_Page_Level::Get_data_mapping_info_for_gc(const stream_id_type stream_id, const LPA_type lpa, PPA_type& ppa, page_status_type& page_state)
	{
		if (domains[stream_id]->Mapping_entry_accessible(ideal_mapping_table, stream_id, lpa)) {
			ppa = domains[stream_id]->Get_ppa(ideal_mapping_table, stream_id, lpa);
			page_state = domains[stream_id]->Get_page_status(ideal_mapping_table, stream_id, lpa);
		} else {
			ppa = domains[stream_id]->GlobalMappingTable[lpa].PPA;
			page_state = domains[stream_id]->GlobalMappingTable[lpa].WrittenStateBitmap;
		}
	}

	inline void Address_Mapping_Unit_Page_Level::Get_translation_mapping_info_for_gc(const stream_id_type stream_id, const MVPN_type mvpn, MPPN_type& mppa, sim_time_type& timestamp)
	{
		mppa = domains[stream_id]->GlobalTranslationDirectory[mvpn].MPPN;
		timestamp = domains[stream_id]->GlobalTranslationDirectory[mvpn].TimeStamp;
	}

	inline MVPN_type Address_Mapping_Unit_Page_Level::get_MVPN(const LPA_type lpn, stream_id_type stream_id)
	{
		//return (MVPN_type)((lpn % (domains[stream_id]->Total_logical_pages_no)) / no_of_translation_entries_per_page);
		return (MVPN_type)(lpn / no_of_translation_entries_per_page);
	}

	inline LPA_type Address_Mapping_Unit_Page_Level::get_start_LPN_in_MVP(const MVPN_type mvpn)
	{
		return (MVPN_type)(mvpn * no_of_translation_entries_per_page);
	}

	inline LPA_type Address_Mapping_Unit_Page_Level::get_end_LPN_in_MVP(const MVPN_type mvpn)
	{
		return (MVPN_type)(mvpn * no_of_translation_entries_per_page + no_of_translation_entries_per_page - 1);
	}

	LPA_type Address_Mapping_Unit_Page_Level::Get_logical_pages_count(stream_id_type stream_id)
	{
		return this->domains[stream_id]->Total_logical_pages_no;
	}
	
	inline NVM::FlashMemory::Physical_Page_Address Address_Mapping_Unit_Page_Level::Convert_ppa_to_address(const PPA_type ppa)
	{
		NVM::FlashMemory::Physical_Page_Address target;
		target.ChannelID = (flash_channel_ID_type)(ppa / page_no_per_channel);
		target.ChipID = (flash_chip_ID_type)((ppa % page_no_per_channel) / page_no_per_chip);
		target.DieID = (flash_die_ID_type)(((ppa % page_no_per_channel) % page_no_per_chip) / page_no_per_die);
		target.PlaneID = (flash_plane_ID_type)((((ppa % page_no_per_channel) % page_no_per_chip) % page_no_per_die) / page_no_per_plane);
		target.BlockID = (flash_block_ID_type)(((((ppa % page_no_per_channel) % page_no_per_chip) % page_no_per_die) % page_no_per_plane) / pages_no_per_block);
		target.PageID = (flash_page_ID_type)((((((ppa % page_no_per_channel) % page_no_per_chip) % page_no_per_die) % page_no_per_plane) % pages_no_per_block) % pages_no_per_block);

		return target;
	}

	inline void Address_Mapping_Unit_Page_Level::Convert_ppa_to_address(const PPA_type ppn, NVM::FlashMemory::Physical_Page_Address& address)
	{
		address.ChannelID = (flash_channel_ID_type)(ppn / page_no_per_channel);
		address.ChipID = (flash_chip_ID_type)((ppn % page_no_per_channel) / page_no_per_chip);
		address.DieID = (flash_die_ID_type)(((ppn % page_no_per_channel) % page_no_per_chip) / page_no_per_die);
		address.PlaneID = (flash_plane_ID_type)((((ppn % page_no_per_channel) % page_no_per_chip) % page_no_per_die) / page_no_per_plane);
		address.BlockID = (flash_block_ID_type)(((((ppn % page_no_per_channel) % page_no_per_chip) % page_no_per_die) % page_no_per_plane) / pages_no_per_block);
		address.PageID = (flash_page_ID_type)((((((ppn % page_no_per_channel) % page_no_per_chip) % page_no_per_die) % page_no_per_plane) % pages_no_per_block) % pages_no_per_block);

	}

	inline PPA_type Address_Mapping_Unit_Page_Level::Convert_address_to_ppa(const NVM::FlashMemory::Physical_Page_Address& pageAddress)
	{
		return (PPA_type)this->page_no_per_chip * (PPA_type)(pageAddress.ChannelID * this->chip_no_per_channel + pageAddress.ChipID)
			+ this->page_no_per_die * pageAddress.DieID + this->page_no_per_plane * pageAddress.PlaneID
			+ this->pages_no_per_block * pageAddress.BlockID + pageAddress.PageID;
	}

	bool Address_Mapping_Unit_Page_Level::request_mapping_entry(const stream_id_type stream_id, const LPA_type lpa)
	{
		AddressMappingDomain* domain = domains[stream_id];
		MVPN_type mvpn = get_MVPN(lpa, stream_id);

		/*This is the first time that a user request accesses this address.
		Just create an entry in cache! No flash read is needed.*/
		if (domain->GlobalTranslationDirectory[mvpn].MPPN == NO_MPPN) {
			if (!domain->CMT->Check_free_slot_availability()) {
				LPA_type evicted_lpa;
				CMTSlotType evictedItem = domain->CMT->Evict_one_slot(evicted_lpa);
				if (evictedItem.Dirty) {
					/* In order to eliminate possible race conditions for the requests that
					* will access the evicted lpa in the near future (before the translation
					* write finishes), MQSim updates GMT (the on flash mapping table) right
					* after eviction happens.*/
					domain->GlobalMappingTable[evicted_lpa].PPA = evictedItem.PPA;
					domain->GlobalMappingTable[evicted_lpa].WrittenStateBitmap = evictedItem.WrittenStateBitmap;
					if (domain->GlobalMappingTable[evicted_lpa].TimeStamp > CurrentTimeStamp)
						throw std::logic_error("Unexpected situation occurred in handling GMT!");
					domain->GlobalMappingTable[evicted_lpa].TimeStamp = CurrentTimeStamp;
					generate_flash_writeback_request_for_mapping_data(stream_id, evicted_lpa);
				}
			}
			domain->CMT->Reserve_slot_for_lpn(stream_id, lpa);
			domain->CMT->Insert_new_mapping_info(stream_id, lpa, NO_PPA, UNWRITTEN_LOGICAL_PAGE);
			
			return true;
		}

		/*A read transaction is already under process to retrieve the MVP content.
		* This situation may happen in two different cases:
		* 1. A read has been issued to retrieve unchanged parts of the mapping data and merge them
		*     with the changed parts (i.e., an update read of MVP). This read will be followed
		*     by a writeback of MVP content to a new flash page.
		* 2. A read has been issued to retrieve the mapping data for some previous user requests*/
		if (domain->ArrivingMappingEntries.find(mvpn) != domain->ArrivingMappingEntries.end())
		{
			if (domain->CMT->Is_slot_reserved_for_lpn_and_waiting(stream_id, lpa)) {
				return false;
			} else { //An entry should be created in the cache
				if (!domain->CMT->Check_free_slot_availability()) {
					LPA_type evicted_lpa;
					CMTSlotType evictedItem = domain->CMT->Evict_one_slot(evicted_lpa);
					if (evictedItem.Dirty) {
						/* In order to eliminate possible race conditions for the requests that
						* will access the evicted lpa in the near future (before the translation
						* write finishes), MQSim updates GMT (the on flash mapping table) right
						* after eviction happens.*/
						domain->GlobalMappingTable[evicted_lpa].PPA = evictedItem.PPA;
						domain->GlobalMappingTable[evicted_lpa].WrittenStateBitmap = evictedItem.WrittenStateBitmap;
						if (domain->GlobalMappingTable[evicted_lpa].TimeStamp > CurrentTimeStamp)
							throw std::logic_error("Unexpected situation occured in handling GMT!");
						domain->GlobalMappingTable[evicted_lpa].TimeStamp = CurrentTimeStamp;
						generate_flash_writeback_request_for_mapping_data(stream_id, evicted_lpa);
					}
				}
				domain->CMT->Reserve_slot_for_lpn(stream_id, lpa);
				domain->ArrivingMappingEntries.insert(std::pair<MVPN_type, LPA_type>(mvpn, lpa));

				return false;
			}
		}

		/*MQSim assumes that the data of all departing (evicted from CMT) translation pages are in memory, until
		the flash program operation finishes and the entry it is cleared from DepartingMappingEntries.*/
		if (domain->DepartingMappingEntries.find(mvpn) != domain->DepartingMappingEntries.end()) {
			if (!domain->CMT->Check_free_slot_availability()) {
				LPA_type evicted_lpa;
				CMTSlotType evictedItem = domain->CMT->Evict_one_slot(evicted_lpa);
				if (evictedItem.Dirty) {
					/* In order to eliminate possible race conditions for the requests that
					* will access the evicted lpa in the near future (before the translation
					* write finishes), MQSim updates GMT (the on flash mapping table) right
					* after eviction happens.*/
					domain->GlobalMappingTable[evicted_lpa].PPA = evictedItem.PPA;
					domain->GlobalMappingTable[evicted_lpa].WrittenStateBitmap = evictedItem.WrittenStateBitmap;
					if (domain->GlobalMappingTable[evicted_lpa].TimeStamp > CurrentTimeStamp)
						throw std::logic_error("Unexpected situation occured in handling GMT!");
					domain->GlobalMappingTable[lpa].TimeStamp = CurrentTimeStamp;
					generate_flash_writeback_request_for_mapping_data(stream_id, evicted_lpa);
				}
			}
			domain->CMT->Reserve_slot_for_lpn(stream_id, lpa);
			/*Hack: since we do not actually save the values of translation requests, we copy the mapping
			data from GlobalMappingTable (which actually must be stored on flash)*/
			domain->CMT->Insert_new_mapping_info(stream_id, lpa,
				domain->GlobalMappingTable[lpa].PPA, domain->GlobalMappingTable[lpa].WrittenStateBitmap);
			
			return true;
		}

		//Non of the above options provide mapping data. So, MQSim, must read the translation data from flash memory
		if (!domain->CMT->Check_free_slot_availability()) {
			LPA_type evicted_lpa;
			CMTSlotType evictedItem = domain->CMT->Evict_one_slot(evicted_lpa);
			if (evictedItem.Dirty) {
				/* In order to eliminate possible race conditions for the requests that
				* will access the evicted lpa in the near future (before the translation
				* write finishes), MQSim updates GMT (the on flash mapping table) right
				* after eviction happens.*/
				domain->GlobalMappingTable[evicted_lpa].PPA = evictedItem.PPA;
				domain->GlobalMappingTable[evicted_lpa].WrittenStateBitmap = evictedItem.WrittenStateBitmap;
				if (domain->GlobalMappingTable[evicted_lpa].TimeStamp > CurrentTimeStamp) {
					throw std::logic_error("Unexpected situation occured in handling GMT!");
				}
				domain->GlobalMappingTable[evicted_lpa].TimeStamp = CurrentTimeStamp;
				generate_flash_writeback_request_for_mapping_data(stream_id, evicted_lpa);
			}
		}
		domain->CMT->Reserve_slot_for_lpn(stream_id, lpa);
		generate_flash_read_request_for_mapping_data(stream_id, lpa);//consult GTD and create read transaction
		
		return false;
	}

	void Address_Mapping_Unit_Page_Level::generate_flash_writeback_request_for_mapping_data(const stream_id_type stream_id, const LPA_type lpn)
	{
		MVPN_type mvpn = get_MVPN(lpn, stream_id);
		if (is_mvpn_locked_for_gc(stream_id, mvpn)) {
			manage_mapping_transaction_facing_barrier(stream_id, mvpn, false);
			domains[stream_id]->DepartingMappingEntries.insert(get_MVPN(lpn, stream_id));
		} else {
			ftl->TSU->Prepare_for_transaction_submit();

			//Writing back all dirty CMT entries that fall into the same translation virtual page (MVPN)
			unsigned int read_size = 0;
			page_status_type readSectorsBitmap = 0;
			LPA_type startLPN = get_start_LPN_in_MVP(mvpn);
			LPA_type endLPN = get_end_LPN_in_MVP(mvpn);
			for (LPA_type lpn_itr = startLPN; lpn_itr <= endLPN; lpn_itr++) {
				if (domains[stream_id]->CMT->Exists(stream_id, lpn_itr)) {
					if (domains[stream_id]->CMT->Is_dirty(stream_id, lpn_itr)) {
						domains[stream_id]->CMT->Make_clean(stream_id, lpn_itr);
						domains[stream_id]->GlobalMappingTable[lpn_itr].PPA = domains[stream_id]->CMT->Retrieve_ppa(stream_id, lpn_itr);
					} else {
						page_status_type bitlocation = (((page_status_type)0x1) << (((lpn_itr - startLPN) * GTD_entry_size) / SECTOR_SIZE_IN_BYTE));
						if ((readSectorsBitmap & bitlocation) == 0) {
							readSectorsBitmap |= bitlocation;
							read_size += SECTOR_SIZE_IN_BYTE;
						}
					}
				}
			}

			//Read the unchaged mapping entries from flash to merge them with updated parts of MVPN
			NVM_Transaction_Flash_RD* readTR = NULL;
			MPPN_type mppn = domains[stream_id]->GlobalTranslationDirectory[mvpn].MPPN;
			if (mppn != NO_MPPN) {
				readTR = new NVM_Transaction_Flash_RD(Transaction_Source_Type::MAPPING, stream_id, read_size,
					mvpn, mppn, NULL, mvpn, NULL, readSectorsBitmap, CurrentTimeStamp);
				Convert_ppa_to_address(mppn, readTR->Address);
				block_manager->Read_transaction_issued(readTR->Address);//Inform block_manager as soon as the transaction's target address is determined
				domains[stream_id]->ArrivingMappingEntries.insert(std::pair<MVPN_type, LPA_type>(mvpn, lpn));
				ftl->TSU->Submit_transaction(readTR);
			}

			NVM_Transaction_Flash_WR* writeTR = new NVM_Transaction_Flash_WR(Transaction_Source_Type::MAPPING, stream_id, SECTOR_SIZE_IN_BYTE * sector_no_per_page,
				mvpn, mppn, NULL, mvpn, readTR, (((page_status_type)0x1) << sector_no_per_page) - 1, CurrentTimeStamp);
			allocate_plane_for_translation_write(writeTR);
			allocate_page_in_plane_for_translation_write(writeTR, mvpn, false);
			domains[stream_id]->DepartingMappingEntries.insert(get_MVPN(lpn, stream_id));
			ftl->TSU->Submit_transaction(writeTR);

			Stats::Total_flash_reads_for_mapping++;
			Stats::Total_flash_writes_for_mapping++;
			Stats::Total_flash_reads_for_mapping_per_stream[stream_id]++;
			Stats::Total_flash_writes_for_mapping_per_stream[stream_id]++;

			ftl->TSU->Schedule();
		}
	}

	void Address_Mapping_Unit_Page_Level::generate_flash_read_request_for_mapping_data(const stream_id_type stream_id, const LPA_type lpn)
	{
		MVPN_type mvpn = get_MVPN(lpn, stream_id);

		if (mvpn >= domains[stream_id]->Total_translation_pages_no) {
			PRINT_ERROR("Out of range virtual translation page number!")
		}

		domains[stream_id]->ArrivingMappingEntries.insert(std::pair<MVPN_type, LPA_type>(mvpn, lpn));

		if (is_mvpn_locked_for_gc(stream_id, mvpn)) {
			manage_mapping_transaction_facing_barrier(stream_id, mvpn, true);
		} else {
			ftl->TSU->Prepare_for_transaction_submit();

			PPA_type ppn = domains[stream_id]->GlobalTranslationDirectory[mvpn].MPPN;

			if (ppn == NO_MPPN){
				PRINT_ERROR("Reading an invalid physical flash page address in function generate_flash_read_request_for_mapping_data!")
			}

			NVM_Transaction_Flash_RD* readTR = new NVM_Transaction_Flash_RD(Transaction_Source_Type::MAPPING, stream_id,
					SECTOR_SIZE_IN_BYTE, NO_LPA, NO_PPA, NULL, mvpn, ((page_status_type)0x1) << sector_no_per_page, CurrentTimeStamp);
			Convert_ppa_to_address(ppn, readTR->Address);
			block_manager->Read_transaction_issued(readTR->Address);//Inform block_manager as soon as the transaction's target address is determined
			readTR->PPA = ppn;
			ftl->TSU->Submit_transaction(readTR);

			Stats::Total_flash_reads_for_mapping++;
			Stats::Total_flash_reads_for_mapping_per_stream[stream_id]++;

			ftl->TSU->Schedule();
		}
	}

	inline void Address_Mapping_Unit_Page_Level::handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash* transaction)
	{
		//First check if the transaction source is Mapping Module
		if (transaction->Source != Transaction_Source_Type::MAPPING) {
			return;
		}

		if (_my_instance->ideal_mapping_table){
			throw std::logic_error("There should not be any flash read/write when ideal mapping is enabled!");
		}

		if (transaction->Type == Transaction_Type::WRITE) {
			_my_instance->domains[transaction->Stream_id]->DepartingMappingEntries.erase((MVPN_type)((NVM_Transaction_Flash_WR*)transaction)->Content);
		} else {
			/*If this is a read for an MVP that is required for merging unchanged mapping enries
			* (stored on flash) with those updated entries that are evicted from CMT*/
			if (((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite != NULL) {
				((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;
			}

			_my_instance->ftl->TSU->Prepare_for_transaction_submit();
			MVPN_type mvpn = (MVPN_type)((NVM_Transaction_Flash_RD*)transaction)->Content;
			std::multimap<MVPN_type, LPA_type>::iterator it = _my_instance->domains[transaction->Stream_id]->ArrivingMappingEntries.find(mvpn);
			while (it != _my_instance->domains[transaction->Stream_id]->ArrivingMappingEntries.end()) {
				if ((*it).first == mvpn) {
					LPA_type lpa = (*it).second;

					//This mapping entry may arrived due to an update read request that is required for merging new and old mapping entries.
					//If that is the case, we should not insert it into CMT
					if (_my_instance->domains[transaction->Stream_id]->CMT->Is_slot_reserved_for_lpn_and_waiting(transaction->Stream_id, lpa)) {
						_my_instance->domains[transaction->Stream_id]->CMT->Insert_new_mapping_info(transaction->Stream_id, lpa,
							_my_instance->domains[transaction->Stream_id]->GlobalMappingTable[lpa].PPA,
							_my_instance->domains[transaction->Stream_id]->GlobalMappingTable[lpa].WrittenStateBitmap);
						auto it2 = _my_instance->domains[transaction->Stream_id]->Waiting_unmapped_read_transactions.find(lpa);
						while (it2 != _my_instance->domains[transaction->Stream_id]->Waiting_unmapped_read_transactions.end() &&
							(*it2).first == lpa) {
							if (_my_instance->is_lpa_locked_for_gc(transaction->Stream_id, lpa)) {
								_my_instance->manage_user_transaction_facing_barrier(it2->second);
							} else {
								if (_my_instance->translate_lpa_to_ppa(transaction->Stream_id, it2->second)) {
									_my_instance->ftl->TSU->Submit_transaction(it2->second);
								}
								else {
									_my_instance->mange_unsuccessful_translation(it2->second);
								}
							}
							_my_instance->domains[transaction->Stream_id]->Waiting_unmapped_read_transactions.erase(it2++);
						}
						it2 = _my_instance->domains[transaction->Stream_id]->Waiting_unmapped_program_transactions.find(lpa);
						while (it2 != _my_instance->domains[transaction->Stream_id]->Waiting_unmapped_program_transactions.end() &&
							(*it2).first == lpa) {
							if (_my_instance->is_lpa_locked_for_gc(transaction->Stream_id, lpa)) {
								_my_instance->manage_user_transaction_facing_barrier(it2->second);
							} else {
								if (_my_instance->translate_lpa_to_ppa(transaction->Stream_id, it2->second)) {
									_my_instance->ftl->TSU->Submit_transaction(it2->second);
									if (((NVM_Transaction_Flash_WR*)it2->second)->RelatedRead != NULL) {
										_my_instance->ftl->TSU->Submit_transaction(((NVM_Transaction_Flash_WR*)it2->second)->RelatedRead);
									}
								} else {
									_my_instance->mange_unsuccessful_translation(it2->second);
								}
							}
							_my_instance->domains[transaction->Stream_id]->Waiting_unmapped_program_transactions.erase(it2++);
						}
					}
				} else {
					break;
				}
				_my_instance->domains[transaction->Stream_id]->ArrivingMappingEntries.erase(it++);
			}
			_my_instance->ftl->TSU->Schedule();
		}
	}

	//Locked_LPAs에 존재하는지 확인
	inline bool Address_Mapping_Unit_Page_Level::is_lpa_locked_for_gc(stream_id_type stream_id, LPA_type lpa)
	{
		return domains[stream_id]->Locked_LPAs.find(lpa) != domains[stream_id]->Locked_LPAs.end();
	}

	inline bool Address_Mapping_Unit_Page_Level::is_mvpn_locked_for_gc(stream_id_type stream_id, MVPN_type mvpn)
	{
		return domains[stream_id]->Locked_MVPNs.find(mvpn) != domains[stream_id]->Locked_MVPNs.end();
	}

	//Locked_LPAs에 추가
	inline void Address_Mapping_Unit_Page_Level::Set_barrier_for_accessing_lpa(stream_id_type stream_id, LPA_type lpa)
	{
		auto itr = domains[stream_id]->Locked_LPAs.find(lpa);
		if (itr != domains[stream_id]->Locked_LPAs.end()) {
			PRINT_ERROR("Illegal operation: Locking an LPA that has already been locked!");
		}
		domains[stream_id]->Locked_LPAs.insert(lpa);
	}

	inline void Address_Mapping_Unit_Page_Level::Set_barrier_for_accessing_mvpn(stream_id_type stream_id, MVPN_type mvpn)
	{
		auto itr = domains[stream_id]->Locked_MVPNs.find(mvpn);
		if (itr != domains[stream_id]->Locked_MVPNs.end()) {
			PRINT_ERROR("Illegal operation: Locking an MVPN that has already been locked!");
		}
		domains[stream_id]->Locked_MVPNs.insert(mvpn);
	}

	//Set_barrier_for_accessing_lpa(mvpn) 호출 담당 + 그전에 메타데이터가 서로 일치하는지 확인
	inline void Address_Mapping_Unit_Page_Level::Set_barrier_for_accessing_physical_block(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		//The LPAs are actually not known until they are read one-by-one from flash storage. But, to reduce MQSim's complexity, we assume that LPAs are stored in DRAM and thus no read from flash storage is needed.
		Block_Pool_Slot_Type* block = &(block_manager->plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID].Blocks[block_address.BlockID]);
		NVM::FlashMemory::Physical_Page_Address addr(block_address);
		for (flash_page_ID_type pageID = 0; pageID < block->Current_page_write_index; pageID++) {
			if (block_manager->Is_page_valid(block, pageID)) {
				addr.PageID = pageID;
				if (block->Holds_mapping_data) {
					MVPN_type mpvn = (MVPN_type)flash_controller->Get_metadata(addr.ChannelID, addr.ChipID, addr.DieID, addr.PlaneID, addr.BlockID, addr.PageID);
					if (domains[block->Stream_id]->GlobalTranslationDirectory[mpvn].MPPN != Convert_address_to_ppa(addr)) {
						PRINT_ERROR("Inconsistency in the global translation directory when locking an MPVN!")
						Set_barrier_for_accessing_mvpn(block->Stream_id, mpvn);
					}
				} else {
					LPA_type lpa = flash_controller->Get_metadata(addr.ChannelID, addr.ChipID, addr.DieID, addr.PlaneID, addr.BlockID, addr.PageID);
					LPA_type ppa = domains[block->Stream_id]->GlobalMappingTable[lpa].PPA;
					//if(lpa == NO_LPA)
					//	continue;
					if (domains[block->Stream_id]->CMT->Exists(block->Stream_id, lpa)) {
						ppa = domains[block->Stream_id]->CMT->Retrieve_ppa(block->Stream_id, lpa);
					}
					if (ppa != Convert_address_to_ppa(addr)) {
						std::cout<<"LPA: "<<lpa<<", PPA: "<<ppa<<std::endl;
						PRINT_ERROR("Inconsistency in the global mapping table when locking an LPA!")
					}
					Set_barrier_for_accessing_lpa(block->Stream_id, lpa);
				}
			}
		}
	}

	inline void Address_Mapping_Unit_Page_Level::Remove_barrier_for_accessing_lpa(stream_id_type stream_id, LPA_type lpa)
	{
		auto itr = domains[stream_id]->Locked_LPAs.find(lpa);
		if (itr == domains[stream_id]->Locked_LPAs.end()) {
			std::cout<<"lpa: "<<lpa<<std::endl;
			PRINT_ERROR("Illegal operation: Unlocking an LPA that has not been locked!");
		}
		domains[stream_id]->Locked_LPAs.erase(itr);

		//If there are read requests waiting behind the barrier, then MQSim assumes they can be serviced with the actual page data that is accessed during GC execution
		auto read_tr = domains[stream_id]->Read_transactions_behind_LPA_barrier.find(lpa);
		while (read_tr != domains[stream_id]->Read_transactions_behind_LPA_barrier.end()) {
			handle_transaction_serviced_signal_from_PHY((*read_tr).second);
			delete (*read_tr).second;
			domains[stream_id]->Read_transactions_behind_LPA_barrier.erase(read_tr);
			read_tr = domains[stream_id]->Read_transactions_behind_LPA_barrier.find(lpa);
		}

		//If there are write requests waiting behind the barrier, then MQSim assumes they can be serviced with the actual page data that is accessed during GC execution. This may not be 100% true for all write requests, but, to avoid more complexity in the simulation, we accept this assumption.
		auto write_tr = domains[stream_id]->Write_transactions_behind_LPA_barrier.find(lpa);
		while (write_tr != domains[stream_id]->Write_transactions_behind_LPA_barrier.end()) {
			handle_transaction_serviced_signal_from_PHY((*write_tr).second);
			delete (*write_tr).second;
			domains[stream_id]->Write_transactions_behind_LPA_barrier.erase(write_tr);
			write_tr = domains[stream_id]->Write_transactions_behind_LPA_barrier.find(lpa);
		}
	}

	inline void Address_Mapping_Unit_Page_Level::Remove_barrier_for_accessing_mvpn(stream_id_type stream_id, MVPN_type mvpn)
	{
		auto itr = domains[stream_id]->Locked_MVPNs.find(mvpn);
		if (itr == domains[stream_id]->Locked_MVPNs.end()) {
			PRINT_ERROR("Illegal operation: Unlocking an MVPN that has not been locked!");
		}
		domains[stream_id]->Locked_MVPNs.erase(itr);

		//If there are read requests waiting behind the barrier, then MQSim assumes they can be serviced with the actual page data that is accessed during GC execution
		if (domains[stream_id]->MVPN_read_transactions_waiting_behind_barrier.find(mvpn) != domains[stream_id]->MVPN_read_transactions_waiting_behind_barrier.end()) {
			domains[stream_id]->MVPN_read_transactions_waiting_behind_barrier.erase(mvpn);
			PPA_type ppn = domains[stream_id]->GlobalTranslationDirectory[mvpn].MPPN;
			if (ppn == NO_MPPN) {
				PRINT_ERROR("Reading an invalid physical flash page address in function generate_flash_read_request_for_mapping_data!")
			}

			NVM_Transaction_Flash_RD* readTR = new NVM_Transaction_Flash_RD(Transaction_Source_Type::MAPPING, stream_id,
					SECTOR_SIZE_IN_BYTE, NO_LPA, NO_PPA, NULL, mvpn, ((page_status_type)0x1) << sector_no_per_page, CurrentTimeStamp);
			Convert_ppa_to_address(ppn, readTR->Address);
			readTR->PPA = ppn;
			Stats::Total_flash_reads_for_mapping++;
			Stats::Total_flash_reads_for_mapping_per_stream[stream_id]++;

			handle_transaction_serviced_signal_from_PHY(readTR);
			
			delete readTR;
		}

		//If there are write requests waiting behind the barrier, then MQSim assumes they can be serviced with the actual page data that is accessed during GC execution. This may not be 100% true for all write requests, but, to avoid more complexity in the simulation, we accept this assumption.
		if (domains[stream_id]->MVPN_write_transaction_waiting_behind_barrier.find(mvpn) != domains[stream_id]->MVPN_write_transaction_waiting_behind_barrier.end()) {
			domains[stream_id]->MVPN_write_transaction_waiting_behind_barrier.erase(mvpn);
			//Writing back all dirty CMT entries that fall into the same translation virtual page (MVPN)
			unsigned int read_size = 0;
			page_status_type readSectorsBitmap = 0;
			LPA_type start_lpn = get_start_LPN_in_MVP(mvpn);
			LPA_type end_lpn = get_end_LPN_in_MVP(mvpn);
			for (LPA_type lpn_itr = start_lpn; lpn_itr <= end_lpn; lpn_itr++) {
				if (domains[stream_id]->CMT->Exists(stream_id, lpn_itr)) {
					if (domains[stream_id]->CMT->Is_dirty(stream_id, lpn_itr)) {
						domains[stream_id]->CMT->Make_clean(stream_id, lpn_itr);
					} else {
						page_status_type bitlocation = (((page_status_type)0x1) << (((lpn_itr - start_lpn) * GTD_entry_size) / SECTOR_SIZE_IN_BYTE));
						if ((readSectorsBitmap & bitlocation) == 0) {
							readSectorsBitmap |= bitlocation;
							read_size += SECTOR_SIZE_IN_BYTE;
						}
					}
				}
			}

			//Read the unchaged mapping entries from flash to merge them with updated parts of MVPN
			MPPN_type mppn = domains[stream_id]->GlobalTranslationDirectory[mvpn].MPPN;
			NVM_Transaction_Flash_WR* writeTR = new NVM_Transaction_Flash_WR(Transaction_Source_Type::MAPPING, stream_id, SECTOR_SIZE_IN_BYTE * sector_no_per_page,
				mvpn, mppn, NULL, mvpn, NULL, (((page_status_type)0x1) << sector_no_per_page) - 1, CurrentTimeStamp);

			Stats::Total_flash_reads_for_mapping++;
			Stats::Total_flash_writes_for_mapping++;
			Stats::Total_flash_reads_for_mapping_per_stream[stream_id]++;
			Stats::Total_flash_writes_for_mapping_per_stream[stream_id]++;

			handle_transaction_serviced_signal_from_PHY(writeTR);

			delete writeTR;
		}
	}

	inline void Address_Mapping_Unit_Page_Level::manage_user_transaction_facing_barrier(NVM_Transaction_Flash* transaction)
	{
		std::pair<LPA_type, NVM_Transaction_Flash*> entry(transaction->LPA, transaction);
		if (transaction->Type == Transaction_Type::READ) {
			domains[transaction->Stream_id]->Read_transactions_behind_LPA_barrier.insert(entry);
		} else {
			domains[transaction->Stream_id]->Write_transactions_behind_LPA_barrier.insert(entry);
		}
	}

	inline void Address_Mapping_Unit_Page_Level::manage_mapping_transaction_facing_barrier(stream_id_type stream_id, MVPN_type mvpn, bool read)
	{
		if (read) {
			domains[stream_id]->MVPN_read_transactions_waiting_behind_barrier.insert(mvpn);
		} else {
			domains[stream_id]->MVPN_write_transaction_waiting_behind_barrier.insert(mvpn);
		}
	}

	//translate_lpa_and_dispatch()쪽에서 stop_servicing_write에 의해 호출
	//Write_transactions_for_overfull_planes에 transaction 추가
	void Address_Mapping_Unit_Page_Level::mange_unsuccessful_translation(NVM_Transaction_Flash* transaction)
	{
		//Currently, the only unsuccessfull translation would be for program translations that are accessing a plane that is running out of free pages
		Write_transactions_for_overfull_planes[transaction->Address.ChannelID][transaction->Address.ChipID][transaction->Address.DieID][transaction->Address.PlaneID].insert((NVM_Transaction_Flash_WR*)transaction);
	}

	//void GC_and_WL_Unit_Base::handle_transaction_serviced_signal_from_PHY에서 호출
	void Address_Mapping_Unit_Page_Level::Start_servicing_writes_for_overfull_plane(const NVM::FlashMemory::Physical_Page_Address plane_address)
	{
		std::set<NVM_Transaction_Flash_WR*>& waiting_write_list = Write_transactions_for_overfull_planes[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID];

		ftl->TSU->Prepare_for_transaction_submit();
		auto program = waiting_write_list.begin();
		while (program != waiting_write_list.end()) {
			if (translate_lpa_to_ppa((*program)->Stream_id, *program)) {
				ftl->TSU->Submit_transaction(*program);
				if ((*program)->RelatedRead != NULL) {
					ftl->TSU->Submit_transaction((*program)->RelatedRead);
				}
				waiting_write_list.erase(program++);
			}
			else {
				break;
			}
		}
		ftl->TSU->Schedule();
	}

	void Address_Mapping_Unit_Page_Level::returnVictimPages(std::vector<LPA_type> &v, unsigned int num_pages)
	{
		
	}
}
