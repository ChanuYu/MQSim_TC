#include "Tiering_Area_Controller_Base.h"

namespace SSD_Components
{
    Tiering_Area_Controller_Base* Tiering_Area_Controller_Base::_my_instance = NULL;
    Tiering_Area_Controller_Base::Tiering_Area_Controller_Base(GC_and_WL_Unit_Base *gc_wl, Address_Mapping_Unit_Base *amu, Flash_Block_Manager_Base *block_manager, TSU_Base *tsu, NVM_PHY_ONFI* flash_controller, Host_Interface_Base *hil,/*unsigned short *p_on_the_fly_requests,*/
                                    unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page)
                                    :gc_wl(gc_wl), amu(amu), block_manager(block_manager),tsu(tsu), flash_controller(flash_controller), hil(hil),/*p_on_the_fly_requests(p_on_the_fly_requests),*/
                                    channel_count(channel), chip_per_channel(chip), die_per_chip(die), plane_per_die(plane), block_per_plane(block), page_per_block(page), cur_util(0.0),
                                    ADJUST_THRESHOLD(page_per_block * PAGE_SIZE)
    {
        _my_instance = this;
        total_block_counts = channel_count * chip_per_channel * die_per_chip * plane_per_die * block_per_plane;
        //current_free_blocks = total_block_counts;
        total_capacity = total_block_counts * page_per_block * PAGE_SIZE;
        current_ideal_slc_area_size = 2;

        channel_status = new ChannelStatus_BI[channel_count];
        chip_status = new ChipStatus_BI[channel_count * chip_per_channel];

        //NVM_PHY_ONFI에서 오는 시그널 핸들러 등록
        flash_controller->ConnectToChannelIdleSignal(handle_channel_idle_signal);
        flash_controller->ConnectToChipIdleSignal(handle_chip_idle_signal);
        flash_controller->ConnectToTransactionServicedSignal(handle_transaction_serviced_signal);

        flash_controller->ConnectToChannelBusySignal(handle_channel_busy_signal);
        flash_controller->ConnectToChipBusySignal(handle_chip_busy_signal);

        //Host Interface Logic으로부터 no request signal이 발생할 경우 처리할 핸들러를 등록
        hil->input_stream_manager->ConnectToNoRequestSignal(handleNoRequestSignal);
    }
    Tiering_Area_Controller_Base::~Tiering_Area_Controller_Base()
    {
        delete[] channel_status;
        delete[] chip_status;
    }
    /*
    unsigned short Tiering_Area_Controller_Base::getOnTheFlyRequests()
    {
        return *p_on_the_fly_requests;
    }
    */
    unsigned int Tiering_Area_Controller_Base::getIdealSLCAreaSize()
    {
        return current_ideal_slc_area_size;
    }

    void Tiering_Area_Controller_Base::handle_chip_busy_signal(NVM::FlashMemory::Flash_Chip *chip)
    {
        _my_instance->chip_status[chip->ChipID] = ChipStatus_BI::BUSY;
    }

    void Tiering_Area_Controller_Base::handle_chip_busy_signal(flash_chip_ID_type chip)
    {
        _my_instance->chip_status[chip] = ChipStatus_BI::BUSY;
    }

    void Tiering_Area_Controller_Base::handle_channel_busy_signal(flash_channel_ID_type channel)
    {
        _my_instance->channel_status[channel] = ChannelStatus_BI::BUSY;
    }

    void Tiering_Area_Controller_Base::handle_chip_idle_signal(NVM::FlashMemory::Flash_Chip *chip)
    {
        _my_instance->chip_status[chip->ChipID] = ChipStatus_BI::IDLE;
    }

    void Tiering_Area_Controller_Base::handle_chip_idle_signal(flash_chip_ID_type chip)
    {
        _my_instance->chip_status[chip] = ChipStatus_BI::IDLE;
    }
    
    void Tiering_Area_Controller_Base::handle_channel_idle_signal(flash_channel_ID_type channel)
    {
        _my_instance->channel_status[channel] = ChannelStatus_BI::IDLE;
    }

    /**
     * 1. 매트랜잭션이 처리될 때마다 write trx를 수행하였다면 transaction이 처리된 plane_address의 slc 영역을 확인 (tlc영역은 gc 모듈에서 수행)
     * 2. tlc 트랜잭션이라면 현재 plane_address의 tlc 영역 크기가 부족한지 확인 (gc 역치에 가까운지 확인), 부족하다면  
     *         -> slc 영역을 줄일 수 있으면 줄여서 tlc 영역 확보
     *         -> 줄일 수 없다면 slc 영역의 gc 수행
     *         -> 마이그레이션 수행 이후 강제로 tlc 전환
     * 3. plane_address의 
    */
    void Tiering_Area_Controller_Base::handle_transaction_serviced_signal(NVM_Transaction_Flash* transaction)
    {
        if(transaction->Type!=Transaction_Type::WRITE)
            return;

        PlaneBookKeepingType *pbke = _my_instance->block_manager->Get_plane_bookkeeping_entry(transaction->Address);
        //double util = _my_instance->getCurrentUtilization();

        //slc 영역 GC 혹은 마이그레이션 호출
        if(transaction->isSLCTrx) {
            


        } else {  //tlc의 영역이 부족한 경우 slc 영역의 크기를 줄여야 함

        }

    }

    void Tiering_Area_Controller_Base::handleNoRequestSignal()
    {

    }
}