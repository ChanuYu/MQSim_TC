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
        //flash_controller->ConnectToChannelIdleSignal(handle_channel_idle_signal);
        //flash_controller->ConnectToChipIdleSignal(handle_chip_idle_signal);
        flash_controller->ConnectToTransactionServicedSignal(handle_transaction_serviced_signal);

        //flash_controller->ConnectToChannelBusySignal(handle_channel_busy_signal);
        //flash_controller->ConnectToChipBusySignal(handle_chip_busy_signal);

        //Host Interface Logic으로부터 no request signal이 발생할 경우 처리할 핸들러를 등록
        hil->input_stream_manager->ConnectToNoRequestSignal(handleNoRequestSignal);

        //NVM_PHY_ONFI에서 broadcast를 하면 일단 tsu측에서 칩에 대기 중인 트랜잭션을 다시 스케줄링함
        //즉, service_read/write/erase 함수를 수행함
        //남아있는 트랜잭션이 없는 경우 tac모듈로 chip idle signal을 재전파함
        tsu->ConnectToChipIdleSignal(handle_chip_idle_signal);
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

    SelectedAction Tiering_Area_Controller_Base::selectAction(NVM::FlashMemory::Flash_Chip *chip)
    {

    }

    /**
     * tsu에서 먼저 대기 중인 리퀘스트를 다 처리하려고 시도
     * -> 대기 중인 리퀘스트가 없을 경우 tac로 idle signal 브로드캐스트
     * 핫데이터 티어링 혹은 콜드데이터 압축을 수행
    */
    void Tiering_Area_Controller_Base::handle_chip_idle_signal(NVM::FlashMemory::Flash_Chip *chip)
    {
        //티어링을 먼저 할지, 압축을 먼저 할지 정함 (조건 검토)
        SelectedAction action = _my_instance->selectAction(chip);

        switch(action)
        {
            case SelectedAction::TIERING:
                _my_instance->executeHotdataTiering(chip);
                break;
            case SelectedAction::COMPRESSION:
                _my_instance->executeCompression(chip);
                break;
        }
    }

    void Tiering_Area_Controller_Base::handle_chip_idle_signal(flash_chip_ID_type chip) {}
    
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
     * =======================================================================================================
     * 언제 slc 영역을 줄일지, 줄일 때 마이그레이션 담당 함수 호출
     * 현재 tlc 사이즈와 현재 공간사용률에 따른 이상적 tlc 사이즈를 비교하여
     * 그 차이가 slc block 하나를 줄여서 얻을 수 있는 용량이면 마이그레이션 수행
    */
    void Tiering_Area_Controller_Base::handle_transaction_serviced_signal(NVM_Transaction_Flash* transaction)
    {
        //if(transaction->Type!=Transaction_Type::WRITE)
        //    return;

        PlaneBookKeepingType *pbke = _my_instance->block_manager->Get_plane_bookkeeping_entry(transaction->Address);
        unsigned int free_block_pool_size = pbke->Get_free_block_pool_size(false);
        //double util = _my_instance->getCurrentUtilization();
        //Block_Pool_Slot_Type *block;
        //slc 영역 GC 혹은 마이그레이션 호출
        if(free_block_pool_size < _my_instance->gc_wl->block_pool_gc_threshold - 1) {
            if(!pbke->slc_blocks.size())
                return; //이미 slc 영역이 존재하지 않음
            NVM::FlashMemory::Physical_Page_Address block_address = transaction->Address;
            block_address.BlockID = pbke->slc_block_history.front();
            //block = &pbke->Blocks[block_id];

            if (pbke->Ongoing_erase_operations.find(block_address.BlockID) != pbke->Ongoing_erase_operations.end()) {
                PRINT_ERROR("TAC::GC operation has already operated on the block")
                return;
            }
            
            //gc 모듈의 handle_transaction_serviced_signal()에서 slc 영역 gc를 수행하게 될 경우
            //해당 블록에 대한 migrate를 하면 안 됨
            //=> 스케줄링에서 후순위인 migration에서 발행된 트랜잭션이 먼저 수행되는 결과를 초래할 수 있음
            //해결: on_going_gc_wl 플래그가 true이면 gc를 수행하므로 리턴
            //이 경우 마이그레이션을 강제로 수행할 것인가??
            //gc를 우선 수행하고 마이그레이션을 미룬다면 성능을 더 높일 수 있으니 일단 return 방식
            if (_my_instance->block_manager->Block_has_ongoing_gc_wl(transaction->Address))
                return;

            _my_instance->migrate(block_address, pbke);
        }
        
        
    }

    void Tiering_Area_Controller_Base::handleNoRequestSignal()
    {

    }
}