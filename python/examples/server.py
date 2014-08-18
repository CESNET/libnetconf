#!/usr/bin/python3
# -*- coding:utf-8 -*-

import netconf

log = open("/tmp/pynetconf.log", "w")

# accept NETCONF connection
session = netconf.Session.accept()

while session.isActive() == True:
	session.processRequest()

log.write("Done\n")
log.close()

