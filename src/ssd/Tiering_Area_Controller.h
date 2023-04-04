#ifndef TIERING_AREA_CONTROLLER_H
#define TIERING_AREA_CONTROLLER_H

#include "Tiering_Area_Controller_Base.h"

namespace SSD_Components
{
    class Tiering_Area_Controller : public Tiering_Area_Controller_Base
    {
    public:
        Tiering_Area_Controller(GC_and_WL_Unit_Base *gc_wl, Address_Mapping_Unit_Base *amu, Flash_Block_Manager_Base *block_manager, TSU_Base *tsu, NVM_PHY_ONFI* flash_controller, Host_Interface_Base *hil,/*unsigned short *p_on_the_fly_requests,*/
                                    unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page);
        ~Tiering_Area_Controller();
        void handleNoRequestSignal();

        unsigned int getCurrentSLCAreaSize(PlaneBookKeepingType *pbke);
        double getCurrentUtilization();
        void broadcastIdealSLCAreaSize(PlaneBookKeepingType *pbke);

        bool needToAdjustSLCArea();
        void adjustSLCArea(unsigned int);
        void increaseSLCArea(unsigned int change_amount);
        void decreaseSLCArea(unsigned int change_amount);
        bool needToMigrate(PlaneBookKeepingType *pbke);
        void migrate(std::vector<LPA_type> &victim_pages);
        void getVictimPages(std::vector<LPA_type> &victim_pages, PlaneBookKeepingType *pbke);
    
        int calculateSpaceToBeAdjusted(double util);
    private:
        double getIdealSLCRatio(double util);
        unsigned int getIdealSLCAreaSize(double util);
        unsigned long getIdealTLCAreaSize(double util);
    };
}

#endif