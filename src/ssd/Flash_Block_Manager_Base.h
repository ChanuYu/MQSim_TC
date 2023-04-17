#ifndef BLOCK_POOL_MANAGER_BASE_H
#define BLOCK_POOL_MANAGER_BASE_H

#include <list>
#include <cstdint>
#include <queue>
#include <set>
#include "../nvm_chip/flash_memory/FlashTypes.h"
#include "../nvm_chip/flash_memory/Physical_Page_Address.h"
#include "GC_and_WL_Unit_Base.h"
#include "../nvm_chip/flash_memory/FlashTypes.h"

#include "Tiering_Area_Controller_Base.h"

namespace SSD_Components
{
#define All_VALID_PAGE 0x0000000000000000ULL
	class GC_and_WL_Unit_Base;
	class Flash_Mode_Controller;
	class Tiering_Area_Controller_Base;
	/*
	* Block_Service_Status is used to impelement a state machine for each physical block in order to
	* eliminate race conditions between GC page movements and normal user I/O requests.
	* Allowed transitions:
	* 1: IDLE -> GC, IDLE -> USER
	* 2: GC -> IDLE, GC -> GC_UWAIT
	* 3: USER -> IDLE, USER -> GC_USER
	* 4: GC_UWAIT -> GC, GC_UWAIT -> GC_UWAIT
	* 5: GC_USER -> GC
	*/
	enum class Block_Service_Status {IDLE, GC_WL, USER, GC_USER, GC_UWAIT, GC_USER_UWAIT};
	enum class ExecutionStatus {SUCCESS, FAIL, NONE};

	class Block_Pool_Slot_Type
	{
	public:
		flash_block_ID_type BlockID;
		flash_page_ID_type Current_page_write_index;
		Block_Service_Status Current_status;
		unsigned int Invalid_page_count;
		unsigned int Erase_count;
		static unsigned int Page_vector_size; //블록 내의 페이지를 표현하는 데 쓰이는 uint64_t Invalid_page_bitmap 배열의 사이즈 (4로 초기화)
		uint64_t* Invalid_page_bitmap;//A bit sequence that keeps track of valid/invalid status of pages in the block. A "0" means valid, and a "1" means invalid.
		stream_id_type Stream_id = NO_STREAM;
		bool Holds_mapping_data = false;
		bool Has_ongoing_gc_wl = false;
		NVM_Transaction_Flash_ER* Erase_transaction;
		bool Hot_block = false;//Used for hot/cold separation mentioned in the "On the necessity of hot and cold data identification to reduce the write amplification in flash-based SSDs", Perf. Eval., 2014.
		int Ongoing_user_read_count;
		int Ongoing_user_program_count;
		void Erase();
		
		//23.03.02
		bool isSLC = false;
		flash_page_ID_type Last_page_index; //default: pages_no_per_block - 1 (index임)  
		
		
		void changeFlashMode(bool toSLC); //Free_block_pool <---> free_slc_block 전용 인터페이스 (Free block에서만 호출되어야 함)
	};

	class PlaneBookKeepingType
	{
	public:
		unsigned int Total_pages_count;
		unsigned int Free_pages_count;	//slc로 전환할 경우 감소
		unsigned int Valid_pages_count;
		unsigned int Invalid_pages_count; //slc로 전환할 경우 증가
		Block_Pool_Slot_Type* Blocks;  //모든 기본 플래시 모드(TLC) block
		std::multimap<unsigned int, Block_Pool_Slot_Type*> Free_block_pool;		//unsigned int => erase count
		Block_Pool_Slot_Type** Data_wf, ** GC_wf; //The write frontier blocks for data and GC pages. MQSim adopts Double Write Frontier approach for user and GC writes which is shown very advantages in: B. Van Houdt, "On the necessity of hot and cold data identification to reduce the write amplification in flash - based SSDs", Perf. Eval., 2014
		Block_Pool_Slot_Type** Translation_wf; //The write frontier blocks for translation GC pages

		/**두 개 스트림(멀티스트림)까지만 호환가능한데 성능 안좋을 가능성 큼
		 * 성능테스트의 경우 단일스트림으로 확인하기
		 * 
		 * Free_block_pool <---> free_slc_blocks <---> Data_wf_slc <---> slc_blocks
		*/
		std::map<flash_block_ID_type,Block_Pool_Slot_Type*> slc_blocks; //slc모드로 programmed된 블록을 관리
		std::multimap<unsigned int, Block_Pool_Slot_Type*> free_slc_blocks; //아직 program되지 않은 slc블록 관리
		Block_Pool_Slot_Type** Data_wf_slc;  //**GC_wf_slc; //두 개 스트림(멀티스트림)까지만 호환가능
		std::queue<flash_block_ID_type> slc_block_history;
		Block_Pool_Slot_Type * latest_data_wf_slc;

		//GC victim block 선정방식이 FIFO가 아니라면 신경 쓸 필요 x <- TLC Compression을 위해 고려해야 함 (SLC/TLC 구분해서 관리)
		std::queue<flash_block_ID_type> Block_usage_history;//A fifo queue that keeps track of flash blocks based on their usage history
		std::set<flash_block_ID_type> Ongoing_erase_operations;
		
		//free_slc_blocks <---> Data(GC)_wf_slc
		Block_Pool_Slot_Type* Get_a_free_block(stream_id_type stream_id, bool for_mapping_data, bool for_slc = false);
		unsigned int Get_free_block_pool_size(bool slc_area = false);
		void Check_bookkeeping_correctness(const NVM::FlashMemory::Physical_Page_Address& plane_address);
		void Add_to_free_block_pool(Block_Pool_Slot_Type* block, bool consider_dynamic_wl);
		
		//Free_block_pool <---> free_slc_blocks => FBM::transformToSLCBlocks()

		/**
		 * 23.03.02
		 * 기존의 Free_block_pool에 모든 free block이 들어있다 가정, Flash Mode Controller에 의해 SLC 영역 조정 (확대/축소)
		*/
		unsigned int getCurNumOfSLCBlocks();
		void setNumOfSLCBlocks(unsigned int);

		//플레인 내에서 SLC 블록인지 확인
		bool isBlockInSLCPool(flash_block_ID_type);
	private:
		unsigned int curNumOfSLCBlocks;
	};

	class Flash_Block_Manager_Base
	{
		friend class Address_Mapping_Unit_Page_Level;
		friend class GC_and_WL_Unit_Page_Level;
		friend class GC_and_WL_Unit_Base;
	public:
		Flash_Block_Manager_Base(GC_and_WL_Unit_Base* gc_and_wl_unit, unsigned int max_allowed_block_erase_count, unsigned int total_concurrent_streams_no,
			unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
			unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int initial_slc_blk);
		virtual ~Flash_Block_Manager_Base();
		virtual void Allocate_block_and_page_in_plane_for_user_write(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& address) = 0;
		virtual void Allocate_block_and_page_in_plane_for_user_write(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& address, bool &isSLC) = 0;
		virtual void Allocate_block_and_page_in_plane_for_gc_write(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& address) = 0;
		virtual void Allocate_block_and_page_in_plane_for_translation_write(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& address, bool is_for_gc) = 0;
		virtual void Allocate_Pages_in_block_and_invalidate_remaining_for_preconditioning(const stream_id_type stream_id, const NVM::FlashMemory::Physical_Page_Address& plane_address, std::vector<NVM::FlashMemory::Physical_Page_Address>& page_addresses) = 0;
		virtual void Invalidate_page_in_block(const stream_id_type streamID, const NVM::FlashMemory::Physical_Page_Address& address) = 0;
		virtual void Invalidate_page_in_block_for_preconditioning(const stream_id_type streamID, const NVM::FlashMemory::Physical_Page_Address& address) = 0;
		virtual void Add_erased_block_to_pool(const NVM::FlashMemory::Physical_Page_Address& address) = 0;
		virtual unsigned int Get_pool_size(const NVM::FlashMemory::Physical_Page_Address& plane_address, bool is_slc = false) = 0;
		flash_block_ID_type Get_coldest_block_id(const NVM::FlashMemory::Physical_Page_Address& plane_address, bool is_slc = false);
		unsigned int Get_min_max_erase_difference(const NVM::FlashMemory::Physical_Page_Address& plane_address);
		void Set_GC_and_WL_Unit(GC_and_WL_Unit_Base* );
		void setFlashModeController(Flash_Mode_Controller*);
		PlaneBookKeepingType* Get_plane_bookkeeping_entry(const NVM::FlashMemory::Physical_Page_Address& plane_address);
		bool Block_has_ongoing_gc_wl(const NVM::FlashMemory::Physical_Page_Address& block_address);//Checks if there is an ongoing gc for block_address
		bool Can_execute_gc_wl(const NVM::FlashMemory::Physical_Page_Address& block_address);//Checks if the gc request can be executed on block_address (there shouldn't be any ongoing user read/program requests targeting block_address)
		void GC_WL_started(const NVM::FlashMemory::Physical_Page_Address& block_address);//Updates the block bookkeeping record
		void Cancel_GC_WL_started(const NVM::FlashMemory::Physical_Page_Address& block_address);
		void GC_WL_finished(const NVM::FlashMemory::Physical_Page_Address& block_address);//Updates the block bookkeeping record
		void Read_transaction_issued(const NVM::FlashMemory::Physical_Page_Address& page_address);//Updates the block bookkeeping record
		void Read_transaction_serviced(const NVM::FlashMemory::Physical_Page_Address& page_address);//Updates the block bookkeeping record
		void Program_transaction_serviced(const NVM::FlashMemory::Physical_Page_Address& page_address);//Updates the block bookkeeping record
		bool Is_having_ongoing_program(const NVM::FlashMemory::Physical_Page_Address& block_address);//Cheks if block has any ongoing program request
		bool Is_page_valid(Block_Pool_Slot_Type* block, flash_page_ID_type page_id);//Make the page invalid in the block bookkeeping record
		
		//Free_block_pool <---> free_slc_blocks => FBM::transformToSLCBlocks()
		ExecutionStatus transformToSLCBlocks(PlaneBookKeepingType *pbke, unsigned int num, bool consider_dynamic_wl = false);
		ExecutionStatus transformToTLCBlocks(PlaneBookKeepingType *pbke, unsigned int num, bool consider_dynamic_wl = false);

		//모든 플레인이 동일한 slc 블록 수를 갖고 있는 것은 아님 => 대략적인 현상황을 알리는 척도로만 사용
		//현재 플레인별 블록수를 어떻게 맞출지는 정하지 않음
		unsigned int getCurrSLCBlocksPerPlane() {return current_slc_blocks_per_plane;}
	protected:
		PlaneBookKeepingType ****plane_manager;//Keeps track of plane block usage information
		GC_and_WL_Unit_Base *gc_and_wl_unit;
		Flash_Mode_Controller *FMC;
		unsigned int max_allowed_block_erase_count;
		unsigned int total_concurrent_streams_no;
		unsigned int channel_count;
		unsigned int chip_no_per_channel;
		unsigned int die_no_per_chip;
		unsigned int plane_no_per_die;
		unsigned int block_no_per_plane;
		unsigned int pages_no_per_block;
		void program_transaction_issued(const NVM::FlashMemory::Physical_Page_Address& page_address);//Updates the block bookkeeping record
	
		unsigned int initial_slc_blk_per_plane;
		unsigned int current_slc_blocks_per_plane;
	};
}

#endif//!BLOCK_POOL_MANAGER_BASE_H
