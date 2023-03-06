#ifndef FLASH_MODE_CONTROLLER_H
#define FLASH_MODE_CONTROLLER_H

#include "FTL.h"

/**
 * 모듈설명: IDLE 상태일 때 plane별 SLC 블록 수를 조정 
*/

namespace SSD_Components
{
    enum class TransitionType {SLCTOTLC, TLCTOSLC};

    class Flash_Mode_Controller : public MQSimEngine::Sim_Object
    {
    public:
        Flash_Mode_Controller(const sim_object_id_type& id, FTL*ftl,
			Address_Mapping_Unit_Base* address_mapping_unit, Flash_Block_Manager_Base* block_manager, TSU_Base* tsu, NVM_PHY_ONFI* flash_controller,
			unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
			unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int sector_no_per_page, unsigned int initial_slc_block_per_plane, bool consider_dynamic_wl);

        time_t getNextRequestTime();


        void Start_simulation();
        void Validate_simulation_config();
        void Execute_simulator_event(MQSimEngine::Sim_Event*);

        void transformToSLC(Block_Pool_Slot_Type *);
        int increaseSLCBlocks(PlaneBookKeepingType *,int);
        int decreaseSLCBlocks(PlaneBookKeepingType *,int);
        
        void insertIntoSLCPool(PlaneBookKeepingType *);

        inline int calculateNumChangedPages(int);
        void adjustPageCountStatistics(PlaneBookKeepingType *, int, TransitionType); //SLC로 전환함으로 인해 줄어드는 페이지 수 반영

        const unsigned int initial_slc_block_per_plane;
        const bool consider_dynamic_wl;

    private:
        static Flash_Mode_Controller * _my_instance;
        Address_Mapping_Unit_Base * address_mapping_unit;
        Flash_Block_Manager_Base* block_manager;
		TSU_Base* tsu;
		NVM_PHY_ONFI* flash_controller;
        FTL* ftl;


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