/*
    MIT Licence
    Copyright (c) 2024 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
        * Neither the name of the organization nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.
        * Military use is not permited.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL Julien Verneuil BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "../base/base.h"
#include "../base/config.h"
#include "../base/hardware.h"
#include "../base/plugins_settings.h"
#include "../base/ctrl_preferences.h"
#include "../radio/radiolink.h"
#include "../common/string_utils.h"
#include "events.h"
#include "shared_vars.h"
#include "timers.h"
#include "local_stats.h"
#include "ruby_central.h"
#include "pairing.h"
#include "notifications.h"
#include "handle_commands.h"
#include "osd_common.h"
#include "osd.h"
#include "osd_stats.h"
#include "osd_warnings.h"
#include "osd_widgets.h"
#include "link_watch.h"
#include "ui_alarms.h"
#include "warnings.h"
#include "colors.h"
#include "menu.h"
#include "menu_update_vehicle.h"
#include "menu_confirmation.h"
#include "process_router_messages.h"
#include "fonts.h"


void onModelAdded(u32 uModelId)
{
   log_line("[Event] Handling event new model added (vehicle UID: %u).", uModelId);
   osd_widgets_on_new_vehicle_added_to_controller(uModelId);
   log_line("[Event] Handled event new model added (vehicle UID: %u). Done.", uModelId);
}

void onModelDeleted(u32 uModelId)
{
   log_line("[Event] Handling event model deleted (vehicle UID: %u).", uModelId);
   deletePluginModelSettings(uModelId);
   save_PluginsSettings();
   g_bGotStatsVideoBitrate = false;
   g_bGotStatsVehicleTx = false;
   log_line("[Event] Handled event model deleted (vehicle UID: %u). Done.", uModelId);
}


void onMainVehicleChanged(bool bRemovePreviousVehicleState)
{
   log_line("[Event] Handling event Main Vehicle Changed...");

   if ( NULL != g_pPopupVideoOverloadAlarm )
   {
      if ( popups_has_popup(g_pPopupVideoOverloadAlarm) )
         popups_remove(g_pPopupVideoOverloadAlarm);
      g_pPopupVideoOverloadAlarm = NULL;
   }

   link_watch_reset();
   
   // Remove all the custom popups (above), before removing all popups (so that pointers to custom popups are invalidated beforehand);
   static bool s_bEventsFirstTimeMainVehicleChanged = true;

   if ( ! s_bEventsFirstTimeMainVehicleChanged )
   {
      popups_remove_all();
      menu_discard_all();
      warnings_remove_all();
      osd_warnings_reset();
   }

   s_bEventsFirstTimeMainVehicleChanged = false;
   g_bIsFirstConnectionToCurrentVehicle = true;
   
   if ( bRemovePreviousVehicleState )
      shared_vars_state_reset_all_vehicles_runtime_info();
   
   if ( NULL == g_pCurrentModel )
      log_softerror_and_alarm("[Event] New main vehicle is NULL.");

   render_all(g_TimeNow);
         
   //g_nTotalControllerCPUSpikes = 0;

   if ( bRemovePreviousVehicleState )
      local_stats_reset_all();

   osd_stats_init();
   
   handle_commands_reset_has_received_vehicle_core_plugins_info();

   g_uPersistentAllAlarmsVehicle = 0;
   g_uTimeLastRadioLinkOverloadAlarm = 0;
   g_bHasVideoDataOverloadAlarm = false;
   g_bHasVideoTxOverloadAlarm = false;
   g_bIsVehicleLinkToControllerLost = false;
   g_iDeltaVideoInfoBetweenVehicleController = 0;
   g_iVehicleCorePluginsCount = 0;
   g_bChangedOSDStatsFontSize = false;
   g_bFreezeOSD = false;

   if ( NULL != g_pCurrentModel )
   {
      u32 scale = g_pCurrentModel->osd_params.osd_preferences[g_pCurrentModel->osd_params.layout] & 0xFF;
      osd_setScaleOSD((int)scale);
   
      u32 scaleStats = (g_pCurrentModel->osd_params.osd_preferences[g_pCurrentModel->osd_params.layout]>>16) & 0x0F;
      osd_setScaleOSDStats((int)scaleStats);

      if ( render_engine_is_raw() )
         loadAllFonts(false);

      log_line("Vehicle radio links: %d", g_pCurrentModel->radioLinksParams.links_count );
      for( int i=0; i<g_pCurrentModel->radioLinksParams.links_count; i++ )
      {
         char szBuff[128];
         str_get_radio_frame_flags_description(g_pCurrentModel->radioLinksParams.link_radio_flags[i], szBuff);
         log_line("Vechile radio link %d radio flags: %s", i+1, szBuff);
      }
   }

   for ( int i=0; i<g_iPluginsOSDCount; i++ )
   {
      if ( NULL == g_pPluginsOSD[i] )
         continue;
      if ( NULL != g_pPluginsOSD[i]->pFunctionOnNewVehicle )
         (*(g_pPluginsOSD[i]->pFunctionOnNewVehicle))(g_pCurrentModel->uVehicleId);
   }
   osd_widgets_on_main_vehicle_changed(g_pCurrentModel->uVehicleId);
   
   warnings_on_changed_vehicle();
   log_line("[Event] Handled event Main Vehicle Changed. Done.");
}

void onEventReboot()
{
   log_line("[Event] Handling event Reboot...");
   ruby_pause_watchdog();
   save_temp_local_stats();
   hardware_sleep_ms(50);
   //pairing_stop();

   log_line("[Event] Handled event Reboot. Done.");
}

void onEventBeforePairing()
{
   log_line("[Event] Handling event BeforePairing...");

   notification_add_start_pairing();

   if ( NULL != g_pCurrentModel )
      osd_set_current_layout_index_and_source_model(g_pCurrentModel, g_pCurrentModel->osd_params.layout);
   else
      osd_set_current_layout_index_and_source_model(NULL, 0);

   osd_set_current_data_source_vehicle_index(0);
   shared_vars_osd_reset_before_pairing();

   g_iCurrentActiveVehicleRuntimeInfoIndex = 0;

   g_bHasVideoDataOverloadAlarm = false;
   g_bHasVideoTxOverloadAlarm = false;
   g_bIsVehicleLinkToControllerLost = false;

   g_bGotStatsVideoBitrate = false;
   g_bGotStatsVehicleTx = false;
   g_bFreezeOSD = false;

   g_CurrentUploadingFile.uFileId = 0;
   g_CurrentUploadingFile.uTotalSegments = 0;
   g_CurrentUploadingFile.szFileName[0] = 0;
   for( int i=0; i<MAX_SEGMENTS_FILE_UPLOAD; i++ )
   {
      g_CurrentUploadingFile.pSegments[i] = NULL;
      g_CurrentUploadingFile.uSegmentsSize[i] = 0;
      g_CurrentUploadingFile.bSegmentsUploaded[i] = false;
   }
   g_bHasFileUploadInProgress = false;

   warnings_remove_all();
   alarms_reset_vehicle();

   shared_vars_state_reset_all_vehicles_runtime_info();

   // First vehicle is always the main vehicle, the next ones are relayed vehicles
   if ( NULL != g_pCurrentModel )
   {
      g_VehiclesRuntimeInfo[0].uVehicleId = g_pCurrentModel->uVehicleId;
      g_VehiclesRuntimeInfo[0].pModel = g_pCurrentModel;
      g_iCurrentActiveVehicleRuntimeInfoIndex = 0;

      if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
      if ( g_pCurrentModel->relay_params.uRelayedVehicleId != 0 )
      {
         g_VehiclesRuntimeInfo[1].uVehicleId = g_pCurrentModel->relay_params.uRelayedVehicleId;
         g_VehiclesRuntimeInfo[1].pModel = findModelWithId(g_VehiclesRuntimeInfo[1].uVehicleId, 3);
      }
   }

   log_current_runtime_vehicles_info();

   if ( (NULL != g_pCurrentModel) && g_pCurrentModel->audio_params.has_audio_device )
   {
      ControllerSettings* pCS = get_ControllerSettings();
      
      hardware_set_audio_output(pCS->iAudioOutputDevice, pCS->iAudioOutputVolume);
   }

   log_line("[Event] Current VID for vehicle runtime info[0] is: %u", g_VehiclesRuntimeInfo[0].uVehicleId);
   log_line("[Event] Handled event BeforePairing. Done.");
}

void onEventPaired()
{
   log_line("[Event] Handling event Paired...");

   if ( NULL != g_pCurrentModel )
      osd_set_current_layout_index_and_source_model(g_pCurrentModel, g_pCurrentModel->osd_params.layout);
   else
      osd_set_current_layout_index_and_source_model(NULL, 0);

   link_watch_reset();

   log_line("[Event] Handled event Paired. Done.");
}

void onEventBeforePairingStop()
{
   log_line("[Event] Handling event Before Pairing Stop...");

   osd_remove_stats_flight_end();

   if ( NULL != g_pCurrentModel )
   if ( g_pCurrentModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
   {
      g_pCurrentModel->relay_params.uCurrentRelayMode = RELAY_MODE_MAIN | RELAY_MODE_IS_RELAY_NODE;
      saveControllerModel(g_pCurrentModel);
   }
   log_line("[Event] Handled event Before Pairing Stop.");
}

void onEventPairingStopped()
{
   log_line("[Event] Handling event Pairing Stopped...");

   g_bSwitchingRadioLink = false;
   
   alarms_remove_all();
   popups_remove_all();
   g_bIsRouterReady = false;
   g_RouterIsReadyTimestamp = 0;


   shared_vars_state_reset_all_vehicles_runtime_info();
   link_reset_reconfiguring_radiolink();
   
   g_bHasVideoDataOverloadAlarm = false;
   g_bHasVideoTxOverloadAlarm = false;

   g_bGotStatsVideoBitrate = false;
   g_bGotStatsVehicleTx = false;
   
   g_bHasVideoDecodeStatsSnapshot = false;

   if ( NULL != g_pPopupVideoOverloadAlarm )
   {
      if ( popups_has_popup(g_pPopupVideoOverloadAlarm) )
         popups_remove(g_pPopupVideoOverloadAlarm);
      g_pPopupVideoOverloadAlarm = NULL;
   }

   osd_warnings_reset();
   warnings_remove_all();
   log_line("[Event] Handled event Pairing Stopped. Done.");
}

void onEventPairingStartReceivingData()
{
   log_line("[Event] Hadling event 'Started receiving data from a vehicle'.");

   if ( NULL != g_pPopupLooking )
   {
      popups_remove(g_pPopupLooking);
      g_pPopupLooking = NULL;
      log_line("Removed popup looking for model (4).");
   }
   
   if ( NULL != g_pPopupLinkLost )
   {
      popups_remove(g_pPopupLinkLost);
      g_pPopupLinkLost = NULL;
      log_line("Removed popup link lost.");
   }

   if ( NULL != g_pPopupWrongModel )
   {
      popups_remove(g_pPopupWrongModel);
      g_pPopupWrongModel = NULL;
      log_line("Removed popup wrong model (3).");
   }

   log_current_runtime_vehicles_info();

   log_line("[Event] Got current runtime ruby telemetry: %s, FC telemetry: %s",
      g_VehiclesRuntimeInfo[g_iCurrentActiveVehicleRuntimeInfoIndex].bGotRubyTelemetryInfo? "yes":"no",
      g_VehiclesRuntimeInfo[g_iCurrentActiveVehicleRuntimeInfoIndex].bGotFCTelemetry? "yes":"no");

   log_line("[Event] Mode 'Must sync settings on link recover' : %s", g_bSyncModelSettingsOnLinkRecover?"yes":"no");
   if ( NULL == g_pCurrentModel )
      log_line("[Event] No current model active.");
   else
      log_line("[Event] Must sync model settings: %s", g_pCurrentModel->b_mustSyncFromVehicle?"yes":"no");
   if ( g_bSyncModelSettingsOnLinkRecover )
   {
      g_bSyncModelSettingsOnLinkRecover = false;
      if ( NULL != g_pCurrentModel )
         g_pCurrentModel->b_mustSyncFromVehicle = true;
   }

   log_line("[Event] Handled event 'Started receiving data from vehicle'.");
}

void onEventArmed(u32 uVehicleId)
{
   log_line("[Event] Handling event OnArmed...");
   log_line("Vehicle %u is Armed", uVehicleId);
   local_stats_on_arm(uVehicleId);
   notification_add_armed(uVehicleId);
   osd_remove_stats_flight_end();
   Preferences* p = get_Preferences();
   if ( NULL != p && p->iStartVideoRecOnArm )
      ruby_start_recording();
   log_line("[Event] Handled event OnArmed. Done.");
}

void onEventDisarmed(u32 uVehicleId)
{
   log_line("[Event] Handling event OnDisarmed...");
   log_line("Vehicle %u is Disarmed", uVehicleId);
   local_stats_on_disarm(uVehicleId);
   notification_add_disarmed(uVehicleId);
   osd_add_stats_flight_end();
   Preferences* p = get_Preferences();
   if ( NULL != p && p->iStopVideoRecOnDisarm )
      ruby_stop_recording();
   log_line("[Event] Handled event OnDisarmed. Done.");
}

bool onEventReceivedModelSettings(u32 uVehicleId, u8* pBuffer, int length, bool bUnsolicited)
{
   log_line("[Event] Handling event OnReceivedModelSettings for VID %u (%d bytes, expected: %s)...", uVehicleId, length, (bUnsolicited?"no":"yes"));

   if ( 0 == uVehicleId )
   {
      log_line("[Event] Received model settings for VID 0. Ignoring it.");
      return false;
   }

   if ( NULL == g_pCurrentModel )
   {
      log_line("[Event] current model is NULL. Ignore received model settings.");
      return false;
   }
   log_current_runtime_vehicles_info();

   Model* pModel = NULL;
   bool bFoundInList = false;
   int iRuntimeIndex = -1;
   for( int i=0; i<MAX_CONCURENT_VEHICLES; i++ )
   {
      if ( g_VehiclesRuntimeInfo[i].uVehicleId != uVehicleId )
         continue;
      if ( NULL != g_VehiclesRuntimeInfo[i].pModel )
         log_line("[Event] Received model settings for runtime info %d, %s", i, g_VehiclesRuntimeInfo[i].pModel->getLongName());
      else
         log_line("[Event] Received model settings for runtime info %d, NULL model.", i);
      pModel = g_VehiclesRuntimeInfo[i].pModel;
      bFoundInList = true;
      iRuntimeIndex = i;
      break;
   }

   if ( (! bFoundInList) || (-1 == iRuntimeIndex) )
   {
      log_softerror_and_alarm("[Event] Received model settings for unknown vehicle who's not in the runtime list. Ignoring it.");
      log_current_runtime_vehicles_info();
      return false;
   }

   if ( NULL == pModel )
   {
      log_softerror_and_alarm("[Event] Received model settings for vehicle who who has no model in the runtime list. Ignoring it.");
      log_current_runtime_vehicles_info();
      return false;
   }

   log_line("[Event] Found model (VID %u) in the runtimelist at position %d", pModel->uVehicleId, iRuntimeIndex);
   if ( (NULL != g_pCurrentModel) && (g_pCurrentModel->uVehicleId == pModel->uVehicleId) )
      log_line("[Event] Found model is current model.");
   else
      log_line("[Event] Found model is not current model (current model VID: %u)", (NULL != g_pCurrentModel)?(g_pCurrentModel->uVehicleId): 0);

   bool bOldAudioEnabled = false;
   osd_parameters_t osd_temp;

   bOldAudioEnabled = pModel->audio_params.enabled;
   memcpy((u8*)&osd_temp, (u8*)&(pModel->osd_params), sizeof(osd_parameters_t));

   FILE* fd = fopen("tmp/last_recv_model.mdl", "wb");
   if ( NULL != fd )
   {
       fwrite(pBuffer, 1, length, fd);
       fclose(fd);
       fd = NULL;
   }
   else
   {
      log_error_and_alarm("Failed to save received vehicle configuration to temp file.");
      log_error_and_alarm("[Event]: Failed to process received model settings from vehicle.");
      return false;
   }

   fd = fopen("tmp/last_recv_model.bak", "wb");
   if ( NULL != fd )
   {
       fwrite(pBuffer, 1, length, fd);
       fclose(fd);
       fd = NULL;
   }
   else
   {
      log_error_and_alarm("Failed to save received vehicle configuration to temp file.");
      log_error_and_alarm("[Event]: Failed to process received model settings from vehicle.");
      return false;
   }

   Model modelTemp;
   if ( ! modelTemp.loadFromFile("tmp/last_recv_model.mdl", true) )
   {
      log_softerror_and_alarm("HCommands: Failed to load the received vehicle model file. Invalid file.");
      log_error_and_alarm("[Event]: Failed to process received model settings from vehicle.");
      warnings_add_error_null_model(1);
      return false;
   }

   // Remove relay flags from radio links flags for vehicles version 7.6 or older (build 79)

   bool bRemoveRelayFlags = false;
   if ( (modelTemp.sw_version>>16) < 79 )
      bRemoveRelayFlags = true;

   if ( bRemoveRelayFlags )
   {
      log_line("Received model settings for vehicle version 7.5 or older. Remove relay flags.");

      for( int i=0; i<modelTemp.radioLinksParams.links_count; i++ )
         modelTemp.radioLinksParams.link_capabilities_flags[i] &= ~(RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY);

      for( int i=0; i<modelTemp.radioInterfacesParams.interfaces_count; i++ )
         modelTemp.radioInterfacesParams.interface_capabilities_flags[i] &= ~(RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY);
   }

   log_line("Current (before update) local model (VID: %u) is in controll mode: %s", pModel->uVehicleId, pModel->is_spectator?"no":"yes");
   log_line("Currently received temp model (VID: %u) is in controll mode: %s", modelTemp.uVehicleId, modelTemp.is_spectator?"no":"yes");
   //if ( modelTemp.is_spectator )
   //    log_softerror_and_alarm("Received model is set as spectator mode.");

   log_line("Currently received temp model is in developer mode: %s, total flights: %u", modelTemp.bDeveloperMode?"yes":"no", modelTemp.m_Stats.uTotalFlights);
   log_line("Currently received temp model osd layout: %d, enabled: %s", modelTemp.osd_params.layout, (modelTemp.osd_params.osd_flags2[modelTemp.osd_params.layout] & OSD_FLAG2_LAYOUT_ENABLED)?"yes":"no");
   log_line("Currently received temp model developer flags: [%s]", str_get_developer_flags(modelTemp.uDeveloperFlags));
   log_line("Received vehicle info has %d radio links.", modelTemp.radioLinksParams.links_count );
   
   bool bRadioChanged = false;
   bool bCameraChanged = false;

   if ( pModel->uVehicleId == modelTemp.uVehicleId )
   {
      if ( modelTemp.iCurrentCamera != pModel->iCurrentCamera )
         bCameraChanged = true;
      else if ( (modelTemp.iCurrentCamera >= 0) && (modelTemp.iCameraCount > 0) && (pModel->iCurrentCamera >= 0) && (pModel->iCameraCount > 0) )
      {
         if ( ( modelTemp.camera_params[modelTemp.iCurrentCamera].iCameraType != pModel->camera_params[pModel->iCurrentCamera].iCameraType ) ||
            (modelTemp.camera_params[modelTemp.iCurrentCamera].iForcedCameraType != pModel->camera_params[pModel->iCurrentCamera].iForcedCameraType ) )
            bCameraChanged = true;
      }
      bRadioChanged = IsModelRadioConfigChanged(&(pModel->radioLinksParams), &(pModel->radioInterfacesParams),
        &(modelTemp.radioLinksParams), &(modelTemp.radioInterfacesParams));
      log_line("Received model has different radio config? %s", bRadioChanged?"yes":"no");

      if ( bRadioChanged )
      {
         bRadioChanged = false;
         if ( pModel->radioLinksParams.links_count != modelTemp.radioLinksParams.links_count )
            bRadioChanged = true;

         if ( pModel->radioInterfacesParams.interfaces_count != modelTemp.radioInterfacesParams.interfaces_count )
            bRadioChanged = true;

         for( int i=0; i<pModel->radioLinksParams.links_count; i++ )
         {
            if ( pModel->radioLinksParams.link_frequency_khz[i] != modelTemp.radioLinksParams.link_frequency_khz[i] )
               bRadioChanged = true;
         }

         if ( bRadioChanged )
         {
            if ( pModel == g_pCurrentModel )
               log_line("Received model has different number of radio links or different frequencies. Must update local radio configuration now.");
            else
               log_line("Received model has different number of radio links or different frequencies.");
         }
      }
   }

   for( int i=0; i<modelTemp.radioLinksParams.links_count; i++ )
   {
      char szBuff[128];
      char szBuffC[128];

      str_get_radio_capabilities_description(modelTemp.radioLinksParams.link_capabilities_flags[i], szBuffC);   
      str_get_radio_frame_flags_description(modelTemp.radioLinksParams.link_radio_flags[i], szBuff);
      log_line("Received vechile info: radio link %d: %s, capabilities flags: %s, radio flags: %s", i+1, str_format_frequency(modelTemp.radioLinksParams.link_frequency_khz[i]), szBuffC, szBuff);
   }


   if ( pModel->uVehicleId != modelTemp.uVehicleId )
   {
      Model* pTempModel = findModelWithId(modelTemp.uVehicleId, 4);
      if ( NULL == pTempModel )
      {
         log_softerror_and_alarm("[Event] Received model settings for unknown vehicle id %u (none in the list).", modelTemp.uVehicleId);
         return false;
      }
      saveControllerModel(&modelTemp);
      warnings_add(modelTemp.uVehicleId, "Received vehicle settings.", g_idIconCheckOK);
      return true;
   }

   log_line("Camera did change on the vehicle %u ? %s", modelTemp.uVehicleId, (bCameraChanged?"Yes":"No"));
   
   bool is_spectator = pModel->is_spectator;

   if ( pModel == g_pCurrentModel )
   {
      log_line("[Event] Update current model...");
      saveControllerModel(&modelTemp);
   }
   else
      saveControllerModel(&modelTemp);

   if ( bUnsolicited )
      warnings_add(pModel->uVehicleId, "Received vehicle settings.", g_idIconCheckOK);
   else
      warnings_add(pModel->uVehicleId, "Got vehicle settings.", g_idIconCheckOK);

   log_line("The vehicle has Ruby version %d.%d (b%d) (%u) and the controller %d.%d (b%d) (%u)", ((pModel->sw_version)>>8) & 0xFF, (pModel->sw_version) & 0xFF, ((pModel->sw_version)>>16), pModel->sw_version, SYSTEM_SW_VERSION_MAJOR, SYSTEM_SW_VERSION_MINOR, SYSTEM_SW_BUILD_NUMBER, (SYSTEM_SW_VERSION_MAJOR*256+SYSTEM_SW_VERSION_MINOR) | (SYSTEM_SW_BUILD_NUMBER<<16) );
   
   bool bMustUpdate = false;  
   if ( ((u32)SYSTEM_SW_VERSION_MAJOR)*(int)256 + (u32)SYSTEM_SW_VERSION_MINOR > (pModel->sw_version & 0xFFFF) )
      bMustUpdate = true;
   if ( ((u32)SYSTEM_SW_VERSION_MAJOR)*(int)256 + (u32)SYSTEM_SW_VERSION_MINOR == (pModel->sw_version & 0xFFFF) )
   if ( SYSTEM_SW_BUILD_NUMBER > (pModel->sw_version >> 16) )
      bMustUpdate = true;
   if ( bMustUpdate )
   {
      char szBuff[256];
      char szBuff2[32];
      char szBuff3[32];
      getSystemVersionString(szBuff2, pModel->sw_version);
      getSystemVersionString(szBuff3, (SYSTEM_SW_VERSION_MAJOR<<8) | SYSTEM_SW_VERSION_MINOR);
      sprintf(szBuff, "Vehicle has Ruby version %s (b%d) and your controller %s (b%d). You should update your vehicle.", szBuff2, pModel->sw_version>>16, szBuff3, SYSTEM_SW_BUILD_NUMBER);
      warnings_add(pModel->uVehicleId, szBuff, 0, NULL, 12);
      bool bArmed = false;
      if ( g_VehiclesRuntimeInfo[iRuntimeIndex].bGotFCTelemetry )
      if ( g_VehiclesRuntimeInfo[iRuntimeIndex].headerFCTelemetry.flags & FC_TELE_FLAGS_ARMED )
         bArmed = true;
      if ( ! bArmed )
      if ( ! isMenuOn() )
      if ( ! g_bMenuPopupUpdateVehicleShown )
      {
          add_menu_to_stack( new MenuUpdateVehiclePopup(-1) );
          g_bMenuPopupUpdateVehicleShown = true;
      }
   }

   pModel->is_spectator = is_spectator;
   pModel->b_mustSyncFromVehicle = false;
   log_line("Set settings synchronised flag to true for vehicle.");
   if ( pModel->is_spectator )
      log_line("Vehicle is spectator!");

   if ( pModel->is_spectator )
      memcpy((u8*)&(pModel->osd_params), (u8*)&osd_temp, sizeof(osd_parameters_t));

   log_line("Current model (VID %u) is in developer mode: %s", g_pCurrentModel->uVehicleId, g_pCurrentModel->bDeveloperMode?"yes":"no");
   log_line("Current model (VID %u) is spectator: %s", g_pCurrentModel->uVehicleId, g_pCurrentModel->is_spectator?"yes":"no");
   log_line("Current model (VID %u) on time: %02d:%02d, total flights: %u", g_pCurrentModel->uVehicleId, g_pCurrentModel->m_Stats.uCurrentOnTime/60, g_pCurrentModel->m_Stats.uCurrentOnTime%60, g_pCurrentModel->m_Stats.uTotalFlights);
   log_line("Received model (VID %u) is in developer mode: %s", pModel->uVehicleId, pModel->bDeveloperMode?"yes":"no");
   log_line("Received model (VID %u) is spectator: %s", pModel->uVehicleId, pModel->is_spectator?"yes":"no");
   log_line("Received model (VID %u) on time: %02d:%02d, flight time: %02d:%02d, total flights: %u", pModel->uVehicleId, pModel->m_Stats.uCurrentOnTime/60, pModel->m_Stats.uCurrentOnTime%60, pModel->m_Stats.uCurrentFlightTime/60, pModel->m_Stats.uCurrentFlightTime%60, pModel->m_Stats.uTotalFlights);
   saveControllerModel(pModel);
   log_line("[Event] Updated current local vehicle with received settings.");

   pModel->logVehicleRadioInfo();

   // Check supported cards

   if ( pModel->uVehicleId == g_pCurrentModel->uVehicleId )
   {
      int countUnsuported = 0;
      for( int i=0; i<pModel->radioInterfacesParams.interfaces_count; i++ )
         if ( 0 == (pModel->radioInterfacesParams.interface_type_and_driver[i] & 0xFF0000) )
            countUnsuported++;
   
      if ( countUnsuported == pModel->radioInterfacesParams.interfaces_count )
      {
         Popup* p = new Popup("No radio interface on your vehicle is fully supported.", 0.3,0.4, 0.5, 6);
         p->setIconId(g_idIconError, get_Color_IconError());
         popups_add_topmost(p);
      }
      else if ( countUnsuported > 0 )
      {
         Popup* p = new Popup("Some radio interfaces on your vehicle are not fully supported.", 0.3,0.4, 0.5, 6);
         p->setIconId(g_idIconWarning, get_Color_IconWarning());
         popups_add_topmost(p);
      }
   }

   if ( pModel->alarms & ALARM_ID_UNSUPORTED_USB_SERIAL )
      warnings_add(pModel->uVehicleId, "Your vehicle has an unsupported USB to Serial adapter. Use brand name serial adapters or ones with  CP2102 chipset. The ones with 340 chipset are not compatible.", g_idIconError);

   if ( pModel->audio_params.enabled )
   {
      if ( ! pModel->audio_params.has_audio_device )
         warnings_add(pModel->uVehicleId, "Your vehicle has audio enabled but no audio capture device", g_idIconError);
      else if ( pModel->uVehicleId == g_pCurrentModel->uVehicleId )
      {
         char szOutput[4096];
         hw_execute_bash_command_raw("aplay -l 2>&1", szOutput );
         if ( NULL != strstr(szOutput, "no soundcards") )
            warnings_add(pModel->uVehicleId, "Your vehicle has audio enabled but your controller can't output audio.", g_idIconError);
      }
   }

   // Check forced camera types
   
   for( int i=0; i<pModel->iCameraCount; i++ )
   {
      log_line("Received camera %d name: [%s]", i, pModel->getCameraName(i));
      if ( pModel->camera_params[i].iForcedCameraType != CAMERA_TYPE_NONE )
      if ( pModel->camera_params[i].iForcedCameraType != pModel->camera_params[i].iCameraType )
      {
         char szBuff[256];
         char szBuff1[128];
         char szBuff2[128];
         str_get_hardware_camera_type_string(pModel->camera_params[i].iCameraType, szBuff1);
         str_get_hardware_camera_type_string(pModel->camera_params[i].iForcedCameraType, szBuff2);
         if ( pModel->iCameraCount > 1 )
            snprintf(szBuff, 255, "Your camera %d is autodetected as %s but you forced to work as %s", i+1, szBuff1, szBuff2);
         else
            snprintf(szBuff, 255, "Your camera is autodetected as %s but you forced to work as %s", szBuff1, szBuff2);
         warnings_add(pModel->uVehicleId, szBuff, g_idIconCamera, get_Color_IconWarning() );
      }
   }
   

   if ( pModel->uVehicleId == g_pCurrentModel->uVehicleId )
   if ( g_bIsFirstConnectionToCurrentVehicle )
   {
      osd_add_stats_total_flights();
      g_bIsFirstConnectionToCurrentVehicle = false;
   }

   bool bMustPair = false;
   if ( pModel->uVehicleId == g_pCurrentModel->uVehicleId )
   if ( pModel->audio_params.enabled != bOldAudioEnabled )
   {
      log_line("Audio enable flag changed. Must repair.");
      bMustPair = true;
   }
   if ( pModel->uVehicleId == g_pCurrentModel->uVehicleId )
   if ( bRadioChanged )
      bMustPair = true;

   if ( bMustPair )
   {
      Popup* p = new Popup("Radio links configuration changed on the vehicle. Updating local radio configuration...", 0.15,0.5, 0.7, 5);
      p->setIconId(g_idIconRadio, get_Color_IconWarning());
      popups_add_topmost(p);

      log_line("[Event] Critical change in radio params on the received model settings. Restart pairing with the vehicle.");
      pairing_stop();
      hardware_sleep_ms(100);
      pairing_start_normal();
      log_line("[Events] Handing of event OnReceivedModelSettings complete.");
      return true;
   }

   log_line("[Event] No critical change in radio params on the received model settings. Just notify components to reload model.");
   
   send_model_changed_message_to_router(MODEL_CHANGED_SYNCHRONISED_SETTINGS_FROM_VEHICLE, 0);

   log_line("[Event] Handled of event OnReceivedModelSettings complete.");
   return true;
}

void onEventRelayModeChanged()
{
   log_line("[Event] Handling event OnRelayModeChanged...");
   log_line("[Event] New relay mode: %d (%s), main VID: %u, relayed VID: %u", g_pCurrentModel->relay_params.uCurrentRelayMode, str_format_relay_mode(g_pCurrentModel->relay_params.uCurrentRelayMode), g_pCurrentModel->uVehicleId, g_pCurrentModel->relay_params.uRelayedVehicleId);
   log_line("[Event] Handled of event OnRelayModeChanged complete.");

   Model* pModel = findModelWithId(g_pCurrentModel->relay_params.uRelayedVehicleId, 5);
   if ( NULL != pModel )
   {
      bool bDifferentResolution = false;
      if ( g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].width != pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].width )
         bDifferentResolution = true;
      if ( g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].height != pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].height )
         bDifferentResolution = true;

      if ( bDifferentResolution )
      {
         static u32 s_uTimeLastWarningRelayDifferentResolution = 0;
         if ( g_TimeNow > s_uTimeLastWarningRelayDifferentResolution + 1000*60 )
         {
            s_uTimeLastWarningRelayDifferentResolution = g_TimeNow;
            char szBuff[256];
            sprintf(szBuff, "The relay and relayed vehicles have different video streams resolutions (%d x %d and %d x %d). Set the same video resolution for both cameras to get best relaying performance.",
               g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].width,
               g_pCurrentModel->video_link_profiles[g_pCurrentModel->video_params.user_selected_video_link_profile].height,      
               pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].width,
               pModel->video_link_profiles[pModel->video_params.user_selected_video_link_profile].height );
            warnings_add(g_pCurrentModel->uVehicleId, szBuff, g_idIconCamera);
         }
      }
   }
}