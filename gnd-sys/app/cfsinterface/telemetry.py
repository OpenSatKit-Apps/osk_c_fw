"""


LICENSE:

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
"""
from __future__ import annotations
from abc import ABC, abstractmethod
import sys
import configparser
import socket
import time
import threading
import traceback
from typing import List
from datetime import datetime

import logging
logger = logging.getLogger(__name__)
if __name__ == '__main__':
    from edsmission import EdsMission
    from edsmission import CfeEdsTarget
else:
    from .edsmission import EdsMission
    from .edsmission import CfeEdsTarget

###############################################################################

class TelemetryMessage:
    """
    Plays the 'subject' role in the Observer design pattern. The interface
    declares a set of methods for managing subscribers. An abstract base
    class is not needed since the events are restricted to telemetry 
    messages.
     
    Contains the most recent telemetry values. This class is intentionally kept simple and
    additional 'business' logic is performed by the observer of this packet.
    """
       
    def __init__(self, app_name, msg_name, app_id):
        
        self.app_name = app_name
        self.msg_name = msg_name
        self.app_id   = app_id
        
        self.update_time = None  # Ground time when FSW tlm received
        self.eds_entry = None
        self.eds_obj   = None
    
        self.observers: List[TelemetryObserver] = []      
      
          
    def attach(self, observer: TelemetryObserver) -> None:
        self.observers.append(observer)

    def detach(self, observer: TelemetryObserver) -> None:
        self.observers.remove(observer)

    def eds_obj(self):
        return (self.eds_obj, update_time)

    def pri_hdr(self):
        return self.eds_obj.CCSDS
        
    def sec_hdr(self):
        return self.eds_obj.Sec

    def payload(self):
        return self.eds_obj.Payload

    def update(self, eds_entry, eds_obj) -> None:
        """
        Trigger an update in each subscriber.
        """
        self.eds_entry = eds_entry
        self.eds_obj   = eds_obj
        self.update_time = datetime.now()
        #print("@DEBUG@eds_entry = " + str(eds_entry))       
        #print("@DEBUG@eds_obj = " + str(eds_obj))       
        logger.debug("TelemetryMessage: Notifying observers...")
        for observer in self.observers:
            observer.update(self)


###############################################################################

class TelemetryObserver(ABC):
    """
    The Observer interface declares the notify method, used by subjects. Unlike
    TelemetryMessage, an abstract base class is defined because there are
    different observer scenarios (i.e. GUI vs scripting) that need to be
    accommodated.
    """

    def __init__(self, tlm_server: TelemetryServer):
        
        self.tlm_server = tlm_server
        

    @abstractmethod
    def update(self, tlm_msg: TelemetryMessage) -> None:
        """
        Receive telemetry updates
        """
        pass


###############################################################################

class TelemetryServer(CfeEdsTarget):
    """
    Manage an EDS-defined telemetry interface. It uses the EdsMission database for telemetry
    message definitions.
    """
    
    def __init__(self, mission, target, host_addr, recv_tlm_port, recv_tlm_timeout):
        super().__init__(mission, target, EdsMission.TELEMETRY_IF, host_addr)

        self.recv_tlm_port    = recv_tlm_port
        self.recv_tlm_timeout = recv_tlm_timeout
        
        self.socket.settimeout(recv_tlm_timeout)
        
        self._recv_tlm_thread = None
        self.port_observer    = None

        self.tlm_messages = {}

        for topic in self.topic_dict:
            if topic != EdsMission.TOPIC_TLM_TITLE_KEY:
                (app_name, tlm_msg_name) = self.parse_topic(topic)
                app_id = self.get_app_id(app_name,tlm_msg_name)
                logger.info("TelemetryServer constructor adding App: %s, Msg %s, Id: %d" % (app_name, tlm_msg_name, app_id))
                self.tlm_messages[app_id] = TelemetryMessage(app_name, tlm_msg_name, app_id)
        
        
    def parse_topic(self, topic_name):
        """
        Assumes the following syntax for a telemetry topic: APP_NAME/Application/TLM_NAME
        """
        topic_token = topic_name.split('/')
        return (topic_token[0], topic_token[2])
    

    def get_tlm_msg_from_topic(self,topic_name):
    
        app_name, tlm_msg_name = self.parse_topic(topic_name)    
        app_id = self.get_app_id(app_name, tlm_msg_name)
        tlm_msg = None
        if app_id in self.tlm_messages:
            tlm_msg = self.tlm_messages[app_id]
        return tlm_msg
            
    def add_tlm_messages(self, tlm_msg_dict):
    
        for msg in tlm_msg_dict:
            self.tlm_messages[tlm_msg_dict[msg].app_id] = tlm_msg_dict[msg]
            

    def add_msg_observer(self, tlm_msg: TelemetryMessage, tlm_msg_observer: TelemetryObserver):
    
        if tlm_msg.app_id in self.tlm_messages:
            self.tlm_messages[tlm_msg.app_id].attach(tlm_msg_observer)
            
        else:
            print("Failed to attach telemetry observer. App ID %d is not in the telemetry server database" % tlm_msg.app_id)


    def remove_msg_observer(self, tlm_msg: TelemetryMessage, tlm_msg_observer: TelemetryObserver):
    
        if tlm_msg.app_id in self.tlm_messages:
            self.tlm_messages[tlm_msg.app_id].detach(tlm_msg_observer)
            
        else:
            print("Failed to detach telemetry observer. App ID %d is not in the telemetry server database" % tlm_msg.app_id)


    def add_port_observer(self, port_observer):
        self.port_observer = port_observer


    def get_app_id(self, app_name, tlm_msg_name):
        """
        #todo: Define a  global invalid app ID value 
        #todo: Where should XML dependencies be defined?
        """
        topic_name = app_name.upper() + '/Application/' + tlm_msg_name.upper()
        app_id = -1 
        if topic_name in self.topic_dict:
            app_id = self.topic_dict[topic_name] + 3 #todo: Clueless on the '+3' but that's what gets generated during runtime
                 
        return app_id
        
            
    def _recv_tlm_handler(self):
        
        logger.info("TelemetryServer started receive telemetry handler thread")

        # Constructor sets a timeout so the thread will terminate if no packets
        while not self._recv_tlm_thread.kill:
            try:
                datagram, host = self.socket.recvfrom(4096) #todo: Allow configurable buffer size
                
                # Only accept datagrams with mimimum length of a telemetry header
                if len(datagram) > 6:
                    if self.port_observer != None:
                        self.port_observer(datagram, host)
                    
                    try:
                        eds_entry, eds_obj = self.eds_mission.decode_message(datagram)
                    
                        app_id = int(eds_obj.CCSDS.AppId)
                        logger.debug("Msg name: %s, Msg Id: %d " % (eds_entry.Name,app_id))
                        if app_id in self.tlm_messages:
                            logger.debug("Calling tlm message update()...")
                            self.tlm_messages[app_id].update(eds_entry, eds_obj)
                    
                    except RuntimeError:
                        logger.error("EDS datagram decode exception. Datagram  = \n %s\n", str(datagram))
                        logger.error(traceback.print_exc())
                        
            except socket.error:
                pass
                #print('Ignored socket error...')
                #time.sleep(0.5)
            
        logger.info("TelemetryServer terminating receive telemetry handler thread")
    
    
    def execute(self):

        self.socket.bind((self.host_addr, self.recv_tlm_port))

        self._recv_tlm_thread = threading.Thread(target=self._recv_tlm_handler)
        
        self._recv_tlm_thread.kill = False

        time.sleep(1.0) #todo: Wait for GUI to init. If cFS running an event message occurs before GUI is up it will crash the system
        
        self._recv_tlm_thread.start()


    def shutdown(self):
        self._recv_tlm_thread.kill = True
        logger.info("Telemetry server shutting down")

    
###############################################################################

class CmdLineTelemetry(TelemetryObserver):
    """
    Command line tool to interact with a user to manually send commands to a cFS target. Helpful
    for informal verification of a system configuration.
    """

    def __init__(self, tlm_server: TelemetryServer, monitor_port = False):
        super().__init__(tlm_server)

        if monitor_port:
            self.tlm_server.add_port_observer(self.process_datagram)
        

        for msg in self.tlm_server.tlm_messages:
            self.tlm_server.add_msg_observer(self.tlm_server.tlm_messages[msg], self)        


    def display_entries(self, base_object, base_name):
        """
        Recursive function that iterates over an EDS object and prints the contents of
        the sub-entries to the screen

        Inputs:
        eds_db - EDS Database
        base_object - The EDS object to iterate over
        base_name - The base name for the sub-entities printed to the screen
        """
        # Array display string
        if (self.tlm_server.eds_mission.lib_db.IsArray(base_object)):
            #print("@DEBUG@display_entries()-array: base_object = " + str(base_object))
            #print("@DEBUG@display_entries()-array: base_name = " + str(base_name))
            for i in range(len(base_object)):
                self.display_entries(base_object[i], f"{base_name}[{i}]")
        # Container display string
        elif (self.tlm_server.eds_mission.lib_db.IsContainer(base_object)):
            #print("@DEBUG@display_entries()-container: base_object = " + str(base_object))
            #print("@DEBUG@display_entries()-container: base_name = " + str(base_name))
            for item in base_object:
                self.display_entries(item[1], f"{base_name}.{item[0]}")
        # Everything else (number, enumeration, string, etc.)
        else:
            print('{:<60} = {}'.format(base_name, base_object))


    def process_datagram(self, datagram, host):

        print(f"Telemetry Packet From: {host[0]}:UDP {host[1]}, {8*len(datagram)} bits :")
        print(self.tlm_server.hex_string(datagram.hex(), 16))
        eds_entry, eds_object = self.tlm_server.eds_mission.decode_message(datagram)
        self.display_entries(eds_object, eds_entry.Name)
        print("\n")
 
    
    def update(self, tlm_msg: TelemetryMessage) -> None:
        """
        Receive telemetry updates
        """
        print("Received telemetry message app ID %d at time %d" % (tlm_msg.app_id, tlm_msg.sec_hdr().Seconds))
        

    def reverse_eng(self):
        """
        
        """
        CFE_ES_HK = [
            0x08, 0x40, 0xD7, 0xC2, 0x00, 0x96, 0x00, 0x10, 0x38, 0x3F, 0x00, 0x1E, 0x00, 0x00, 0xD2, 0x46,
            0x06, 0x07, 0x63, 0x00, 0x05, 0x00, 0x00, 0xFF, 0x01, 0x04, 0x00, 0x63, 0x00, 0x00, 0x0C, 0x00,
            0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x4E, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
            0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]

        eds_entry, eds_obj = self.tlm_server.eds_mission.decode_message(bytes(CFE_ES_HK))
        print("@REVERSE: eds_entry type %s, value: %s" % (str(type(eds_entry)), str(eds_entry)))
        print("@REVERSE: eds_obj type %s, value: %s" % (str(type(eds_obj)), str(eds_obj)))
        
        self.display_entries(eds_obj, eds_entry.Name)

        eds_entry = self.tlm_server.eds_mission.get_database_named_entry('CFE_ES/HousekeepingTlm')
        print("@REVERSE: eds_entry type: %s\n@@dir: %s\n@@value: %s" % (str(type(eds_entry)), str(dir(eds_entry)), str(eds_entry)))
        eds_obj   = eds_entry()
        print("@REVERSE: eds_obj type: %s\n@@dir: %s\n@@value: %s" % (str(type(eds_obj)), str(dir(eds_obj)), str(eds_obj)))
        pri_hdr = eds_obj.CCSDS
        print("@REVERSE: pri_hdr type: %s\n@@dir: %s\n@@value: %s" % (str(type(pri_hdr)), str(dir(pri_hdr)), str(pri_hdr)))        
        sec_hdr = eds_obj.Sec
        print("@REVERSE: sec_hdr type: %s\n@@dir: %s\n@@value: %s" % (str(type(sec_hdr)), str(dir(sec_hdr)), str(sec_hdr)))        
        payload = eds_obj.Payload
        print("@REVERSE: payload type: %s\n@@dir: %s\n@@value: %s" % (str(type(payload)), str(dir(payload)), str(payload)))        
        

###############################################################################
        
def main():
    
    config = configparser.ConfigParser()
    config.read('../cfsat.ini')
    MISSION    = config.get('MISSION','EDS_NAME')
    CFS_TARGET = config.get('CFS_TARGET','EDS_NAME')
    HOST_ADDR  = config.get('CFS_TARGET','HOST_ADDR')
    TLM_PORT   = config.getint('CFS_TARGET','RECV_TLM_PORT')
    CFS_TARGET_TLM_TIMEOUT = config.getint('CFS_TARGET','RECV_TLM_TIMEOUT')
    
    system_string = "Mission: %s, Target: %s, Host: %s, Telemetry Port %d" % (MISSION, CFS_TARGET, HOST_ADDR, TLM_PORT)
    try:
        telemetry_server = TelemetryServer(MISSION, CFS_TARGET, HOST_ADDR, TLM_PORT, CFS_TARGET_TLM_TIMEOUT)
        cmd_line_telemetry = CmdLineTelemetry(telemetry_server, True)
        print ("Telemetry object created for " + system_string)
        
    except RuntimeError:
        print("Error creating telemetry object for " + system_string)
        sys.exit(2)

    #cmd_line_telemetry.reverse_eng()
    telemetry_server.execute()
    

if __name__ == "__main__":
    main()


