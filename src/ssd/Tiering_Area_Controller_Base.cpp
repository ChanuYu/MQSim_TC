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
        current_ideal_slc_area_size = 14;

        channel_status = new ChannelStatus_BI[channel_count];
        chip_status = new ChipStatus_BI[channel_count * chip_per_channel];

        //NVM_PHY_ONFI에서 오는 시그널 핸들러 등록
        flash_controller->ConnectToChannelIdleSignal(handle_channel_idle_signal);
        flash_controller->ConnectToChipIdleSignal(handle_chip_idle_signal);
        flash_controller->ConnectToTransactionServicedSignal(handle_transaction_serviced_signal);

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

    void Tiering_Area_Controller_Base::process_chip_busy_signal(flash_chip_ID_type chip)
    {
        chip_status[chip] = ChipStatus_BI::BUSY;
    }
    void Tiering_Area_Controller_Base::process_channel_busy_signal(flash_channel_ID_type channel)
    {
        channel_status[channel] = ChannelStatus_BI::BUSY;
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

    void Tiering_Area_Controller_Base::handle_transaction_serviced_signal(NVM_Transaction_Flash*)
    {

    }
    void Tiering_Area_Controller_Base::handleNoRequestSignal()
    {

    }
}