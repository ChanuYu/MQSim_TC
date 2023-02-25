#ifndef FLASH_MODE_CONTROLLER_H
#define FLASH_MODE_CONTROLLER_H

#include "../sim/Sim_Object.h"
#include "../nvm_chip/flash_memory/Flash_Chip.h"
#include "../nvm_chip/flash_memory/Physical_Page_Address.h"
#include "Address_Mapping_Unit_Base.h"
#include "Flash_Block_Manager_Base.h"
#include "TSU_Base.h"
#include "NVM_PHY_ONFI.h"

namespace SSD_Components
{

    class Flash_Mode_Controller : public MQSimEngine::Sim_Object
    {
    public:
        Flash_Mode_Controller(const sim_object_id_type& id, 
			Address_Mapping_Unit_Base* address_mapping_unit, Flash_Block_Manager_Base* block_manager, TSU_Base* tsu, NVM_PHY_ONFI* flash_controller,
			unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
			unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int sector_no_per_page);

        void Setup_triggers();
        void Start_simulation();
        void Validate_simulation_config();
        void Execute_simulator_event(MQSimEngine::Sim_Event*);

    };

} //end of SSD_Components




#endif //!FLASH_MODE_CONTROLLER_H