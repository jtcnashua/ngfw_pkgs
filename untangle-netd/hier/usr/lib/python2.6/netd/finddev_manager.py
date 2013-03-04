import os
import sys
import subprocess
import datetime
import traceback
import re
from netd.iptables_util import IptablesUtil
from netd.network_util import NetworkUtil

# This class is responsible for writing /etc/untangle-netd/post-network-hook.d/10-finddev
# based on the settings object passed from sync-settings.py
class FindDevManager:
    defaultFilename = "/etc/untangle-netd/post-network-hook.d/10-finddev"
    logFilename = "/var/log/uvm/finddev.log"
    filename = defaultFilename
    file = None

    def calculate_cmd( self, settings, verbosity=0 ):
        cmd = "/usr/share/untangle-netd/bin/finddev -v"
        for intf in settings['interfaces']['list']:
            if 'interfaceId' not in intf or 'systemDev' not in intf:
                continue
            cmd = cmd + ( " -i %s:%i " % ( intf['systemDev'], int(intf['interfaceId']) ) )

        cmd = re.sub(r"\s+",' ',cmd).strip()
        return cmd;

    def sync_settings( self, settings, prefix="", verbosity=0 ):
        if verbosity > 1: print "FindDevManager: sync_settings()"

        self.filename = prefix + self.defaultFilename
        self.fileDir = os.path.dirname( self.filename )
        if not os.path.exists( self.fileDir ):
            os.makedirs( self.fileDir )

        self.file = open( self.filename, "w+" )
        self.file.write("#!/bin/dash");
        self.file.write("\n\n");

        self.file.write("## Auto Generated on %s\n" % datetime.datetime.now());
        self.file.write("## DO NOT EDIT. Changes will be overwritten.\n");
        self.file.write("\n");
        self.file.write("\n");

        self.file.write("CMD=\"" + self.calculate_cmd( settings ) + "\"\n");
        self.file.write("\n");
                          
        self.file.write("QUEUE_NUM=1979" + "\n");
        self.file.write("\n");

        self.file.write("queue_owner()" + "\n");
        self.file.write("{" + "\n");
        self.file.write("    echo `awk -v queue=$QUEUE_NUM '{ if ( $1 == queue  ) print $2 } ' /proc/net/netfilter/nfnetlink_queue`" + "\n");
        self.file.write("}" + "\n");
        self.file.write("\n");

        self.file.write("start_finddev()" + "\n");
        self.file.write("{" + "\n");
        self.file.write("    ${CMD} > %s 2>&1 &" % self.logFilename + "\n");
        self.file.write("}" + "\n");
        self.file.write("\n");

        self.file.write("current_cmd()" + "\n");
        self.file.write("{" + "\n");
        self.file.write("    if [ -z \"`queue_owner`\" ] ; then return ; fi" + "\n");
        self.file.write("    cat /proc/`queue_owner`/cmdline | tr '\\000' ' '" + "\n");
        self.file.write("}" + "\n");
        self.file.write("\n");

        #self.file.write("echo \"CURRENT QUEUE OWNER:\" `queue_owner`" + "\n");
        #self.file.write("echo \"CURRENT CMD:\" `current_cmd`" + "\n");
        #self.file.write("\n");
        
        self.file.write("if [ ! -z \"`queue_owner`\" ] ; then" + "\n");
        self.file.write("    echo \"Stopping finddev [`queue_owner`].\"" + "\n");
        self.file.write("    kill `queue_owner`" + "\n");
        self.file.write("fi" + "\n");

        self.file.write("if [ -z \"`queue_owner`\" ] ; then" + "\n");
        self.file.write("    start_finddev" + "\n");
        self.file.write("    echo \"Started  finddev [$!].\"" + "\n");
        self.file.write("fi" + "\n");

        #self.file.write("if [ -z \"`queue_owner`\" ] ; then" + "\n");
        #self.file.write("    echo \"Starting finddev...\"" + "\n");
        #self.file.write("    start_finddev" + "\n");
        #self.file.write("elif [ \"`current_cmd`\" != \"${CMD}\" ] ; then" + "\n");
        #self.file.write("    echo \"`current_cmd`\"" + "\n");
        #self.file.write("    echo \"${CMD}\"" + "\n");
        #self.file.write("    echo \"Restarting finddev...\"" + "\n");
        #self.file.write("    kill `queue_owner`" + "\n");
        #self.file.write("    start_finddev" + "\n");
        #self.file.write("else" + "\n");
        #self.file.write("    echo \"Alreading running...\"" + "\n");
        #self.file.write("fi");
        #self.file.write("\n");
        #self.file.write("\n");

        self.file.flush();
        self.file.close();

        os.system("chmod a+x %s" % self.filename)

        if verbosity > 0:
            print "FindDevManager: Wrote %s" % self.filename

        return



    