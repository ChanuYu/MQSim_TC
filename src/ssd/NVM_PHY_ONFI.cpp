#include "NVM_PHY_ONFI.h"

namespace SSD_Components {

	void NVM_PHY_ONFI::ConnectToChannelBusySignal(ChannelBusyHandlerType function)
	{
		connectedChannelBusyHandlers.push_back(function);
	}

	void NVM_PHY_ONFI::broadcastChannelBusySignal(flash_channel_ID_type channel)
	{
		for (std::vector<ChannelBusyHandlerType>::iterator it = connectedChannelBusyHandlers.begin();
			it != connectedChannelBusyHandlers.end(); it++) {
			(*it)(channel);
		}
	}

	void NVM_PHY_ONFI::ConnectToChipBusySignal(ChipBusyHandlerType function)
	{
		connectedChipBusyHandlers.push_back(function);
	}

	void NVM_PHY_ONFI::broadcastChipBusySignal(flash_chip_ID_type chip)
	{
		for (std::vector<ChipBusyHandlerType>::iterator it = connectedChipBusyHandlers.begin();
			it != connectedChipBusyHandlers.end(); it++) {
			(*it)(chip);
		}
	}

	void NVM_PHY_ONFI::ConnectToTransactionServicedSignal(TransactionServicedHandlerType function)
	{
		//handle_transaction_serviced_signal_from_PHY
		connectedTransactionServicedHandlers.push_back(function);
	}

	/*
	* Different FTL components maybe waiting for a transaction to be finished:
	* HostInterface: For user reads and writes
	* Address_Mapping_Unit: For mapping reads and writes
	* TSU: For the reads that must be finished for partial writes (first read non updated parts of page data and then merge and write them into the new page)
	* GarbageCollector: For gc reads, writes, and erases
	* 
	* ##handle_transaction_serviced_signal_from_PHY
	*/
	void NVM_PHY_ONFI::broadcastTransactionServicedSignal(NVM_Transaction_Flash* transaction)
	{
		for (std::vector<TransactionServicedHandlerType>::iterator it = connectedTransactionServicedHandlers.begin();
			it != connectedTransactionServicedHandlers.end(); it++) {
			(*it)(transaction);// it => handle_transaction_serviced_signal_from_PHY
		}
		delete transaction;//This transaction has been consumed and no more needed
	}

	void NVM_PHY_ONFI::ConnectToChannelIdleSignal(ChannelIdleHandlerType function)
	{
		connectedChannelIdleHandlers.push_back(function);
	}

	void NVM_PHY_ONFI::broadcastChannelIdleSignal(flash_channel_ID_type channelID)
	{
		for (std::vector<ChannelIdleHandlerType>::iterator it = connectedChannelIdleHandlers.begin();
			it != connectedChannelIdleHandlers.end(); it++) {
			(*it)(channelID);
		}
	}

	void NVM_PHY_ONFI::ConnectToChipIdleSignal(ChipIdleHandlerType function)
	{
		connectedChipIdleHandlers.push_back(function);
	}

	void NVM_PHY_ONFI::broadcastChipIdleSignal(NVM::FlashMemory::Flash_Chip* chip)
	{
		for (std::vector<ChipIdleHandlerType>::iterator it = connectedChipIdleHandlers.begin();
			it != connectedChipIdleHandlers.end(); it++) {
			(*it)(chip);
		}
	}
}