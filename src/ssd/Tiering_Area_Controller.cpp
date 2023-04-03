#include "Tiering_Area_Controller.h"
#include <cmath>

namespace SSD_Components
{
    
    Tiering_Area_Controller::Tiering_Area_Controller(GC_and_WL_Unit_Base *gc_wl, Address_Mapping_Unit_Base *amu, Flash_Block_Manager_Base *block_manager, TSU_Base *tsu, NVM_PHY_ONFI_NVDDR2* flash_controller, /*unsigned short *p_on_the_fly_requests,*/
                                    unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page)
                                    : Tiering_Area_Controller_Base(gc_wl,amu,block_manager,tsu,flash_controller,/*p_on_the_fly_requests,*/channel,chip,die,plane,block,page) {}

    Tiering_Area_Controller::~Tiering_Area_Controller(){}

    void Tiering_Area_Controller::handleNoRequestSignal()
    {

    }

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

    void Tiering_Area_Controller::adjustSLCArea(bool )
    {

    }

    void Tiering_Area_Controller::increaseSLCArea(unsigned int change_amount)
    {

    }

    void Tiering_Area_Controller::decreaseSLCArea(unsigned int change_amount)
    {

    }

    bool Tiering_Area_Controller::needToMigrate(PlaneBookKeepingType *pbke)
    {
        return false;
    }

    void Tiering_Area_Controller::migrate(std::vector<LPA_type> &victim_pages)
    {

    }

    //amu에 요청하면 amu에서 처리
    void Tiering_Area_Controller::getVictimPages(std::vector<LPA_type> &victim_pages, PlaneBookKeepingType *pbke)
    {

    }

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