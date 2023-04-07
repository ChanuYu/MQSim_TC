#ifndef TIERING_AREA_CONTROLLER_BASE_H
#define TIERING_AREA_CONTROLLER_BASE_H

#include "GC_and_WL_Unit_Base.h"
#include "Host_Interface_Base.h"

namespace SSD_Components
{
    class GC_and_WL_Unit_Base;
    class TSU_Base;
    class Flash_Block_Manager_Base;
    class PlaneBookKeepingType;
    class Block_Pool_Slot_Type;
    class Address_Mapping_Unit_Base;

    #define PAGE_SIZE 4096

    enum class ChannelStatus_BI {BUSY, IDLE};
    enum class ChipStatus_BI {BUSY, IDLE};

    class Tiering_Area_Controller_Base
    {
    public:
        Tiering_Area_Controller_Base(GC_and_WL_Unit_Base *gc_wl, Address_Mapping_Unit_Base *amu, Flash_Block_Manager_Base *block_manager, TSU_Base *tsu, NVM_PHY_ONFI* flash_controller, Host_Interface_Base *hil,/*unsigned short *p_on_the_fly_requests,*/
                                    unsigned int channel=4, unsigned int chip=2, unsigned int die=2, unsigned int plane=2, unsigned int block=1024, unsigned int page=256);
        virtual ~Tiering_Area_Controller_Base();
        //unsigned short getOnTheFlyRequests();
        unsigned int getIdealSLCAreaSize();

        static void handleNoRequestSignal();

        virtual unsigned int getCurrentSLCAreaSize(PlaneBookKeepingType *pbke) = 0;
        virtual double getCurrentUtilization() = 0;
        
        virtual int calculateSpaceToBeAdjusted(double util) = 0;
        virtual void broadcastIdealSLCAreaSize(PlaneBookKeepingType *pbke) = 0;

        virtual bool needToAdjustSLCArea() = 0;
        virtual void adjustSLCArea(unsigned int change_amount) = 0; //change_amount는 Byte단위임
        virtual void increaseSLCArea(unsigned int change_amount) = 0;
        virtual void decreaseSLCArea(unsigned int change_amount) = 0;
        
        virtual bool needToMigrate(PlaneBookKeepingType *pbke) = 0;
        virtual void migrate(std::vector<LPA_type> &victim_pages) = 0;
        virtual void executeSLCGC(NVM_Transaction_Flash*) = 0;

        //amu에서 처리
        virtual void getVictimPages(std::vector<LPA_type> &victim_pages, PlaneBookKeepingType *pbke) = 0;
        
        //HIL에서 on the fly requests가 0이 되거나 새로 추가되거나 할 때 호출
        void setNoRequestFlag() {no_request_on_Q = true;}
        void resetNoRequestFlag() {no_request_on_Q = false;}

        //채널과 칩의 BUSY 상태를 기록 (TSU에서 호출)
        static void handle_chip_busy_signal(NVM::FlashMemory::Flash_Chip *chip);
        static void handle_chip_busy_signal(flash_chip_ID_type chip);
        static void handle_channel_busy_signal(flash_channel_ID_type channel);

        //채널과 칩의 IDLE 상태를 기록 (NVM_PHY_ONFI_NVDDR2에서 broadcast)
        static void handle_chip_idle_signal(NVM::FlashMemory::Flash_Chip *chip);
        static void handle_chip_idle_signal(flash_chip_ID_type chip);
        static void handle_channel_idle_signal(flash_channel_ID_type channel);
        static void handle_transaction_serviced_signal(NVM_Transaction_Flash*);

        GC_and_WL_Unit_Base *gc_wl;
        Address_Mapping_Unit_Base* amu;
		Flash_Block_Manager_Base* block_manager;
		TSU_Base* tsu;
        NVM_PHY_ONFI* flash_controller;
        Host_Interface_Base *hil;

        ChannelStatus_BI *channel_status;
        ChipStatus_BI *chip_status;

    protected:
        static Tiering_Area_Controller_Base *_my_instance;
        unsigned int channel_count;
        unsigned int chip_per_channel;
        unsigned int die_per_chip;
        unsigned int plane_per_die;
        unsigned int block_per_plane;
        unsigned int page_per_block;

        unsigned int total_block_counts;
        unsigned int current_ideal_slc_area_size;
        unsigned long total_capacity;

        unsigned int *p_cur_slc_free_blocks; //fbm에서 관리
        unsigned int *p_cur_tlc_free_blocks; //fbm에서 관리
        unsigned int *p_cur_total_free_blocks;
        unsigned int *p_cumulative_compression; //amu에 추가해야 함, 줄인 페이지 수를 기록한다고 가정
        //unsigned short *p_on_the_fly_requests;
        double cur_util;
        bool no_request_on_Q;

        int calculated_space;

        const unsigned int ADJUST_THRESHOLD;
    };

}

#endif