 /*
---------------------------------------------------------------------------------------
This source file is part of swgANH (Star Wars Galaxies - A New Hope - Server Emulator)
For more information, see http://www.swganh.org


Copyright (c) 2006 - 2008 The swgANH Team

---------------------------------------------------------------------------------------
*/
#include "ChatOpcodes.h"
#include "Mail.h"
#include "Player.h"
#include "StructureManagerChat.h"
#include "TradeManagerChat.h"

#include "ZoneServer/TangibleEnums.h"

#include "LogManager/LogManager.h"
#include "DatabaseManager/Database.h"
#include "DatabaseManager/DataBinding.h"
#include "DatabaseManager/DatabaseResult.h"

#include "Common/atMacroString.h"
#include "Common/DispatchClient.h"
#include "Common/Message.h"
#include "Common/MessageDispatch.h"
#include "Common/MessageFactory.h"

#include "Utils/utils.h"
#include "Utils/Timer.h"

#include <boost/lexical_cast.hpp>

#include <cstring>
#include <ctime>


//=========================================================================================

bool								StructureManagerChatHandler::mInsFlag    = false;
StructureManagerChatHandler*		StructureManagerChatHandler::mSingleton  = NULL;

//=========================================================================================


//======================================================================================================================

StructureManagerChatHandler::StructureManagerChatHandler(Database* database, MessageDispatch* dispatch, ChatManager* chatManager)
{

	mDatabase = database;
	mChatManager = chatManager;
	mMessageDispatch = dispatch;
	mPlayerAccountMap = mChatManager->getPlayerAccountMap();
	//StructureManagerAsyncContainer* asyncContainer;

	mMessageDispatch->RegisterMessageCallback(opIsmHarvesterUpdate,this);
	





	// load our bazaar terminals
	//asyncContainer = new TradeManagerAsyncContainer(TRMQuery_LoadBazaar, 0);
	//mDatabase->ExecuteSqlAsync(this,asyncContainer,"SELECT * FROM commerce_bazaar");

	mTimerQueueProcessTimeLimit = 10;

	//move to Handle dispatch message at some time
	uint32 ServerTimeInterval = 1;

    std::tr1::shared_ptr<Timer> hopper_timer(new Timer(SRMTimer_CheckHarvesterHopper,this,ServerTimeInterval*1000,NULL));
    //std::tr1::shared_ptr<Timer> tick_preserve_timer(new Timer(CMTimer_TickPreserve,this,ServerTimeInterval*10000,NULL));
    //std::tr1::shared_ptr<Timer> check_auctions_timer(new Timer(CMTimer_CheckAuctions,this,ServerTimeInterval*10000,NULL));

    mTimers.push_back(hopper_timer);
    //mTimers.push_back(tick_preserve_timer);
    //mTimers.push_back(check_auctions_timer);
    
}


//======================================================================================================================
StructureManagerChatHandler::~StructureManagerChatHandler()
{
	mInsFlag = false;
	
}
//======================================================================================================================
StructureManagerChatHandler*	StructureManagerChatHandler::Init(Database* database, MessageDispatch* dispatch, ChatManager* chatManager)
{
	if(!mInsFlag)
	{
		mSingleton = new StructureManagerChatHandler(database,dispatch,chatManager);
		mInsFlag = true;
		return mSingleton;
	}
	else
		return mSingleton;

}

//======================================================================================================================
void StructureManagerChatHandler::Shutdown()
{	
	mMessageDispatch->UnregisterMessageCallback(opIsmHarvesterUpdate);
	
}

//======================================================================================================================
void StructureManagerChatHandler::handleDispatchMessage(uint32 opcode, Message* message, DispatchClient* client)
{
	
	switch(opcode)
	{
		case opIsmHarvesterUpdate:
		{
			ProcessAddHarvesterHopperUpdate(message,client);
		}
		break;

		
		default:
			gLogger->logMsgF("StructureManagerChatHandler::handleDispatchMessage: Unhandled opcode %u",MSG_NORMAL,opcode);
		break;
	}
	
}

//=======================================================================================================================


void StructureManagerChatHandler::handleDatabaseJobComplete(void* ref,DatabaseResult* result)
{
	StructureManagerAsyncContainer* asynContainer = (StructureManagerAsyncContainer*)ref;
	Player* player(0);
	 /*
	if (!asynContainer->mClient) 
	{
		gLogger->logMsgF("StructureManagerChatHandler:: No client !!!",MSG_NORMAL);
		return;
	}

	PlayerAccountMap::iterator accIt = mPlayerAccountMap.find(asynContainer->mClient->getAccountId());

	if(accIt != mPlayerAccountMap.end())
		player = (*accIt).second;
	else
	{
		gLogger->logMsgF("StructureManagerChatHandler:: Error getting player from account map %u",MSG_NORMAL,asynContainer->mClient->getAccountId());
		return;
	}

	*/
	

	switch(asynContainer->mQueryType)
	{
		
		//=================================================
		//
		//the structures condition reached zero
		//
		case STRMQuery_StructureMailCondZero:
		{
			structure st;
			
			DataBinding* binding = mDatabase->CreateDataBinding(9);
			binding->addField(DFT_uint64,offsetof(structure,owner),8,0);
			binding->addField(DFT_bstring,offsetof(structure,file),64,1);
			binding->addField(DFT_bstring,offsetof(structure,dir),64,2);
			binding->addField(DFT_float,offsetof(structure,x),4,3);
			binding->addField(DFT_float,offsetof(structure,z),4,4);
			binding->addField(DFT_bstring,offsetof(structure,planet),32,5);
			binding->addField(DFT_uint32,offsetof(structure,maxcondition),4,6);
			binding->addField(DFT_uint32,offsetof(structure,maint),4,7);
			binding->addField(DFT_uint64,offsetof(structure,lastMail),8,8);

			uint64 count;
			
			count = result->getRowCount();
			result->GetNextRow(binding,&st);

			if(st.lastMail < (gTradeManagerChat->getGlobalTickCount() + (24*3600*1000)))
			{
				//last Mail less than 24hours no need to send it again
				mDatabase->DestroyDataBinding(binding);
				return;

			}

			//send the relevant EMail

			atMacroString* aMS = new atMacroString();

			aMS->addMBstf("player_structure","structure_condemned_body");
			aMS->addTTstf(st.file.getAnsi(),st.dir.getAnsi()); 
			aMS->addDI((uint32)(st.maxcondition*(st.maint/168)));
			aMS->addTextModule();

			aMS->setPlanetString(st.planet);
			aMS->setWP(st.x,0,st.z,"Structure");
			aMS->addWaypoint();

			Mail* mail = new Mail();
			mail->setSender(BString("@player_structure:management"));
			mail->setSubject(BString("@player_structure:structure_condemned_subject "));
			mail->setText(BString(""));
			mail->setStatus(MailStatus_New);
			mail->setTime(static_cast<uint32>(time(NULL)));
			mail->setAttachments(aMS->assemble());
		
			gChatManager->sendSystemMailMessage(mail,st.owner);
			
			mDatabase->DestroyDataBinding(binding);

			int8 sql[250];
			// Now update the time of the last EMail
			sprintf(sql,"UPDATE structures SET structures.lastMail = %I64u WHERE ID = %I64u", gTradeManagerChat->getGlobalTickCount(), asynContainer->harvesterID);
			
			mDatabase->ExecuteSqlAsync(this,0,sql);

		}
		break;



		//=================================================
		//
		//the structure is getting damaged
		//
		case STRMQuery_StructureMailDamage:
		{
			structure st;
			
			DataBinding* binding = mDatabase->CreateDataBinding(9);
			binding->addField(DFT_uint64,offsetof(structure,owner),8,0);
			binding->addField(DFT_bstring,offsetof(structure,file),64,1);
			binding->addField(DFT_bstring,offsetof(structure,dir),64,2);
			binding->addField(DFT_float,offsetof(structure,x),4,3);
			binding->addField(DFT_float,offsetof(structure,z),4,4);
			binding->addField(DFT_bstring,offsetof(structure,planet),32,5);
			binding->addField(DFT_uint32,offsetof(structure,maxcondition),4,6);
			binding->addField(DFT_uint32,offsetof(structure,condition),4,7);
			binding->addField(DFT_uint64,offsetof(structure,lastMail),8,8);

			uint64 count;
			count = result->getRowCount();
			result->GetNextRow(binding,&st);
			
			if(st.lastMail < (gTradeManagerChat->getGlobalTickCount() + (24*3600*1000)))
			{
				//last Mail less than 24hours no need to send it again
				mDatabase->DestroyDataBinding(binding);
				return;

			}

			//send the relevant EMail

			atMacroString* aMS = new atMacroString();

			aMS->addMBstf("player_structure","mail_structure_damage");
			aMS->addTTstf(st.file.getAnsi(),st.dir.getAnsi()); 
			aMS->addDI((uint32)((st.maxcondition-st.condition)/(st.maxcondition/100)));
			aMS->addTextModule();

			aMS->setPlanetString(st.planet);
			aMS->setWP(st.x,0,st.z,"Structure");
			aMS->addWaypoint();

			Mail* mail = new Mail();
			mail->setSender(BString("@player_structure:management"));
			mail->setSubject(BString("@player_structure:mail_structure_damage_sub"));
			mail->setText(BString(""));
			mail->setStatus(MailStatus_New);
			mail->setTime(static_cast<uint32>(time(NULL)));
			mail->setAttachments(aMS->assemble());
		
			gChatManager->sendSystemMailMessage(mail,st.owner);
			
			mDatabase->DestroyDataBinding(binding);

			int8 sql[250];
			// Now update the time of the last EMail
			sprintf(sql,"UPDATE structures SET structures.lastMail = %I64u WHERE ID = %I64u", gTradeManagerChat->getGlobalTickCount(), asynContainer->harvesterID);
			
			mDatabase->ExecuteSqlAsync(this,0,sql);

		}
		break;


		//=================================================
		//
		//the structure is out of maintenance - the bank account will be used
		//
		case STRMQuery_StructureMailOOFMaint:
		{
			
			structure st;
			
			DataBinding* binding = mDatabase->CreateDataBinding(7);
			binding->addField(DFT_uint64,offsetof(structure,owner),8,0);
			binding->addField(DFT_bstring,offsetof(structure,file),64,1);
			binding->addField(DFT_bstring,offsetof(structure,dir),64,2);
			binding->addField(DFT_float,offsetof(structure,x),4,3);
			binding->addField(DFT_float,offsetof(structure,z),4,4);
			binding->addField(DFT_bstring,offsetof(structure,planet),32,5);
			binding->addField(DFT_uint64,offsetof(structure,lastMail),8,6);

			uint64 count;
			count = result->getRowCount();
			result->GetNextRow(binding,&st);

			if(st.lastMail < (gTradeManagerChat->getGlobalTickCount() + (24*3600*1000)))
			{
				//last Mail less than 24hours no need to send it again
				mDatabase->DestroyDataBinding(binding);
				return;

			}

			
			//send the relevant EMail

			atMacroString* aMS = new atMacroString();

			aMS->addMBstf("player_structure","structure_maintenance_empty_body");
			aMS->addTTstf(st.file.getAnsi(),st.dir.getAnsi()); 
			aMS->addTextModule();

			aMS->setPlanetString(st.planet);
			aMS->setWP(st.x,0,st.z,"Structure");
			aMS->addWaypoint();

			Mail* mail = new Mail();
			mail->setSender(BString("@player_structure:management"));
			mail->setSubject(BString("@player_structure:structure_maintenance_empty_subject"));
			mail->setText(BString(""));
			mail->setStatus(MailStatus_New);
			mail->setTime(static_cast<uint32>(time(NULL)));
			mail->setAttachments(aMS->assemble());
		
			gChatManager->sendSystemMailMessage(mail,st.owner);
			
			mDatabase->DestroyDataBinding(binding);

			int8 sql[250];
			// Now update the time of the last EMail
			sprintf(sql,"UPDATE structures SET structures.lastMail = %I64u WHERE ID = %I64u", gTradeManagerChat->getGlobalTickCount(), asynContainer->harvesterID);
			
			mDatabase->ExecuteSqlAsync(this ,0 ,sql);


		}
		break;

		case STRMQuery_DoneHarvesterMaintenance:
		{
			uint32 exitCode;
			DataBinding* binding = mDatabase->CreateDataBinding(1);
			binding->addField(DFT_uint32,0,4);

			uint64 count;
			count = result->getRowCount();

			//return codes :
			// 0 everything ok
			// 1 structure is out of maintenance  - taking maint out of the bank
			// 2 structure got damaged
			// 3 condition is zero


			result->GetNextRow(binding,&exitCode);

			if(exitCode == 1)// 1 structure is out of maintenance
			{
				// get the Owners ID
				int8 sql[500];

				//start by using power
				sprintf(sql,"SELECT s.owner, st.stf_file, st.stf_name, s.x, s.z, p.name, s.lastMail FROM structures s INNER JOIN structure_type_data st ON (s.type = st.type) INNER JOIN planet p ON (p.planet_id = s.zone)WHERE ID = %I64u",asynContainer->harvesterID);
				StructureManagerAsyncContainer* asyncContainer = new StructureManagerAsyncContainer(STRMQuery_StructureMailOOFMaint,0);
				asyncContainer->harvesterID = asynContainer->harvesterID;
				//gLogger->logMsgF("StructureManagerChatHandler:: %s",MSG_NORMAL,sql);
				
				mDatabase->ExecuteSqlAsync(this,asyncContainer,sql);

			}

			if(exitCode == 2)// 2 structure got damaged
			{
				// get the Owners ID
				int8 sql[500];

				//start by using power
				sprintf(sql,"SELECT s.owner, st.stf_file, st.stf_name, s.x, s.z, p.name, st.max_condition, s.condition, s.lastMail FROM structures s INNER JOIN structure_type_data st ON (s.type = st.type) INNER JOIN planet p ON (p.planet_id = s.zone)WHERE ID = %I64u",asynContainer->harvesterID);
				StructureManagerAsyncContainer* asyncContainer = new StructureManagerAsyncContainer(STRMQuery_StructureMailDamage,0);
				asyncContainer->harvesterID = asynContainer->harvesterID;
				//gLogger->logMsgF("StructureManagerChatHandler:: %s",MSG_NORMAL,sql);
				
				mDatabase->ExecuteSqlAsync(this,asyncContainer,sql);

			}

			if(exitCode == 3)// 1 structure is out of maintenance
			{
				// get the Owners ID
				int8 sql[500];

				//start by using power
				sprintf(sql,"SELECT s.owner, st.stf_file, st.stf_name, s.x, s.z, p.name, st.max_condition, st.maint_cost_wk, s.lastMail FROM structures s INNER JOIN structure_type_data st ON (s.type = st.type) INNER JOIN planet p ON (p.planet_id = s.zone)WHERE ID = %I64u",asynContainer->harvesterID);
				StructureManagerAsyncContainer* asyncContainer = new StructureManagerAsyncContainer(STRMQuery_StructureMailCondZero,0);
				asyncContainer->harvesterID = asynContainer->harvesterID;
				//gLogger->logMsgF("StructureManagerChatHandler:: %s",MSG_NORMAL,sql);
				
				mDatabase->ExecuteSqlAsync(this,asyncContainer,sql);

			}


			if(exitCode > 3)
			{
				//unspecified db error
				gLogger->logMsgF("StructureManagerChat::HarvesterMaintenanceUsage %I64u unspecified db error",MSG_HIGH,asynContainer->harvesterID);
			}
			

			mDatabase->DestroyDataBinding(binding);

		}
		break;

		case STRMQuery_DoneHarvesterUsePower:
		{
			uint32 exitCode;
			DataBinding* binding = mDatabase->CreateDataBinding(1);
			binding->addField(DFT_uint32,0,4);

			uint64 count;
			count = result->getRowCount();

			//return codes :
			// 0 everything ok
			// 1 structure is out of power
			// 2 
			// 3 unspecified db error


			result->GetNextRow(binding,&exitCode);

			// cant notify zone when no player is online
			// zone will update harvesterstatus once per hour
				
			// only reason for us tzo handle this event here  would be if we wanted to inform the player

			if(exitCode == 3)
			{
				//unspecified db error
				gLogger->logMsgF("StructureManagerChat::HarvesterPowerUsage %I64u unspecified db error",MSG_HIGH,asynContainer->harvesterID);
			}
			

			mDatabase->DestroyDataBinding(binding);

		}
		break;

		case STRMQuery_DoneHarvestUpdate:
		{
			uint32 exitCode;
			DataBinding* binding = mDatabase->CreateDataBinding(1);
			binding->addField(DFT_uint32,0,4);

			uint64 count;
			count = result->getRowCount();

			//return codes :
			// 0 everything ok
			// 1 hopper full harvester turned of
			// 2 resource isnt active anymore
			// 3 resource doesnt exist in the first place


			for(uint64 i=0;i <count;i++)
			{
				result->GetNextRow(binding,&exitCode);

				if(exitCode == 3)
				{
					//resource never existed in the first place
					gLogger->logMsgF("StructureMabagerChat::Harvester %I64u harvested an invalid resource",MSG_HIGH,asynContainer->harvesterID);
				}

			}

			mDatabase->DestroyDataBinding(binding);

		}
		break;

		case STRMQuery_HopperUpdate:
		{
			uint64 harvesterID;
			DataBinding* binding = mDatabase->CreateDataBinding(1);
			binding->addField(DFT_uint64,0,8);

			uint64 count;
			count = result->getRowCount();

			for(uint64 i=0;i <count;i++)
			{
				result->GetNextRow(binding,&harvesterID);

				int8 sql[100];

				//start by using power
				sprintf(sql,"SELECT sf_HarvesterUsePower(%I64u)",harvesterID);
				StructureManagerAsyncContainer* asyncContainer = new StructureManagerAsyncContainer(STRMQuery_DoneHarvesterUsePower,0);
				asyncContainer->harvesterID = harvesterID;
				//gLogger->logMsgF("StructureManagerChatHandler:: %s",MSG_NORMAL,sql);
				
				mDatabase->ExecuteSqlAsync(this,asyncContainer,sql);


				// then use maintenance
				sprintf(sql,"SELECT sf_HarvesterUseMaintenance(%I64u)",harvesterID);
				asyncContainer = new StructureManagerAsyncContainer(STRMQuery_DoneHarvesterMaintenance,0);
				asyncContainer->harvesterID = harvesterID;
				//gLogger->logMsgF("StructureManagerChatHandler:: %s",MSG_NORMAL,sql);
				
				mDatabase->ExecuteSqlAsync(this,asyncContainer,sql);


				//now harvest
				sprintf(sql,"SELECT sf_HarvestResource(%I64u)",harvesterID);
				asyncContainer = new StructureManagerAsyncContainer(STRMQuery_DoneHarvestUpdate,0);
				asyncContainer->harvesterID = harvesterID;
				//gLogger->logMsgF("StructureManagerChatHandler:: %s",MSG_NORMAL,sql);
				
				mDatabase->ExecuteSqlAsync(this,asyncContainer,sql);

				//return codes :
				// 0 everything ok
				// 1 hopper full harvester turned of
				// 2 resource isnt active anymore
				// 3 resource doesnt exist in the first place

			}
			
			mDatabase->DestroyDataBinding(binding);

		}
		break;
		
	default:break;
	}
	SAFE_DELETE(asynContainer);

}

//=======================================================================================================================

//=======================================================================================================================


//=======================================================================================================================
void StructureManagerChatHandler::handleTimer(uint32 id, void* container)
{
    boost::mutex::scoped_lock (mTimerMutex);
	mTimerEventQueue.push(id);
}

//=======================================================================================================================
void StructureManagerChatHandler::processTimerEvents()
{
	uint64	startTime = Anh_Utils::Clock::getSingleton()->getLocalTime();
	uint64	processTime = 0;
	while(mTimerEventQueue.size() && processTime < mTimerQueueProcessTimeLimit)
	{
        boost::mutex::scoped_lock lock(mTimerMutex);
		uint32 id = mTimerEventQueue.front();
		mTimerEventQueue.pop();
        lock.unlock();

		switch(id)
		{			
			case SRMTimer_CheckHarvesterHopper:
				{
				handleCheckHarvesterHopper();
				}
			break;

			default:
				gLogger->logMsgF("WorldManager::processTimerEvents: Unknown Timer %u",MSG_HIGH,id);
			break;
		}

		processTime = Anh_Utils::Clock::getSingleton()->getLocalTime() - startTime;
	}
}

//=======================================================================================================================
//
// iterates through the list of hoppers we need to update on a regular basis
// and reads the relevant resource data - then sends it to the client
//
void StructureManagerChatHandler::handleCheckHarvesterHopper()
{

	StructureManagerAsyncContainer* asyncContainer = new StructureManagerAsyncContainer(STRMQuery_HopperUpdate,0);

	int8 sql[100];
	sprintf(sql,"SELECT h.ID FROM harvesters h WHERE h.active > 0 ");
	
	mDatabase->ExecuteSqlAsync(this,asyncContainer,sql);

}

//=======================================================================================================================
void StructureManagerChatHandler::Process()
{
	processTimerEvents();
}


//=======================================================================================================================
//
// This adds or deletes a harvester to /from the hopperupdate list
// All harvesters on this list will have a hopperupdate send every few seconds
//
void StructureManagerChatHandler::ProcessAddHarvesterHopperUpdate(Message* message,DispatchClient* client)
{
	mPlayerAccountMap = mChatManager->getPlayerAccountMap();
	Player* player;
	PlayerAccountMap::iterator accIt = mPlayerAccountMap.find(client->getAccountId());

	if(accIt != mPlayerAccountMap.end())
		player = (*accIt).second;
	else
	{
		gLogger->logMsgF("StructureManagerChatHandler::ProcessAddHarvesterHopperUpdate Error getting player from account map %u",MSG_NORMAL,client->getAccountId());
		return;
	}

	uint64	harvesterID		= message->getUint64();
	uint8   activation		= message->getUint8();
	uint32  updateCounter	= message->getUint32();

	if(activation)
	{
		HarvesterList* hList = getHarvesterList();

		HarvesterItem* item = new(HarvesterItem);
		item->HarvesterID		= harvesterID;
		item->PlayerAccount		= client->getAccountId();
		item->UpdateCounter		= updateCounter;

		hList->push_back(item);
	}
	else
	{

		//TODO update the zone on the new update counter
		HarvesterList* hList = getHarvesterList();
		HarvesterList::iterator it = hList->begin();
		while(it != hList->end())
		{
			if((*it)->HarvesterID == harvesterID)
			{
				it = hList->erase(it);
				return;
			}
			it++;
		}

	}
}


/*
Harvesters
when a harvester gets placed the relevant attributes of the deed are copied over to the harvester.
these attributes are :

381 examine_hoppersize
382 examine_maintenance
383 examine_maintenance_rate
384 examine_power
397 extractrate
1004 harvester_efficiency
1005 harvester_maintenance
1006 harvester_speed

forget 1004 to 1006 were using the former set

then the harvester is created in the db by putting the structure details in the structure table and the 
harvester details - like active / current resource in the harvesters table
*/