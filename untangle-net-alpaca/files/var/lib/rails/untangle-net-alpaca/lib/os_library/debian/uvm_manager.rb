#
# $HeadURL$
# Copyright (c) 2007-2008 Untangle, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2,
# as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
# NONINFRINGEMENT.  See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
class OSLibrary::Debian::UvmManager < OSLibrary::UvmManager
  ## Review : Many if not all of the generated iptables scripts contain zero variables,
  ## and don't actually need to be generated on the fly.

  include Singleton

  IPTablesCommand = OSLibrary::Debian::PacketFilterManager::IPTablesCommand
  
  Chain = OSLibrary::Debian::PacketFilterManager::Chain

  ## uvm subscription file
  UvmSubscriptionFile = "#{OSLibrary::Debian::PacketFilterManager::ConfigDirectory}/700-uvm"

  ## list of rules for the UVM (before the firewall (those rules should override these rules)).
  UvmServicesFile = "#{OSLibrary::Debian::PacketFilterManager::ConfigDirectory}/475-uvm-services"

  ## list of rules for openvpn
  UvmOpenVPNFile = "#{OSLibrary::Debian::PacketFilterManager::ConfigDirectory}/475-openvpn-pf"

  ## UVM interface properties file
  UvmInterfaceProperties = "/etc/untangle-net-alpaca/interface.properties"
  UvmInterfaceOrderProperty = "com.untangle.interface-order"
  
  ## Function that contains all of the subscription / bypass rules
  BypassRules = "bypass_rules"

  ## Script to tell the UVM that the configuration has changed.
  UvmUpdateConfiguration = "/usr/share/untangle-net-alpaca/scripts/uvm/update-configuration"

  def register_hooks
    os["packet_filter_manager"].register_hook( 100, "uvm_manager", "write_files", :hook_write_files )
    os["network_manager"].register_hook( 100, "uvm_manager", "write_files", :hook_write_files )

    ## Register with the hostname manager to update when there are
    ## changes to the hostname
    os["hostname_manager"].register_hook( 1000, "uvm_manager", "commit", :hook_update_configuration )
    os["dns_server_manager"].register_hook( 1000, "uvm_manager", "commit", :hook_update_configuration )
  end

  ## Write out the files to load all of the iptables rules necessary to queue traffic.
  def hook_write_files
    ## These are all of the rules that are used to vector traffic to the UVM.
    write_subscription_script
    ## These are all of the rules to filter / accept traffic to the various services
    ## the UVM provides (80, 443, etc)
    write_packet_filter_script
    write_openvpn_script
    write_interface_order
  end

  ## A helper function for the packet filter manager.
  def handle_custom_rule( rule )
    case rule.system_id
      when "accept-internal-uvm-ede0af7d"
        raise "mismatch type" unless rule.is_a? 
      when "accept-internal-uvm-ede0af7d"
      else return nil
    end

    return nil
  end

  ## Tell the UVM that there has been a change in the alpaca settings.  This is only used when something
  ## Changes but doesn't call the network manager.
  def hook_update_configuration
    run_command( UvmUpdateConfiguration )
  end
  
  private
  
  def write_subscription_script
    text = header
    
    text += subscription_rules
    
    text += <<EOF
HELPER_SCRIPT="/usr/share/untangle-net-alpaca/scripts/uvm/iptables"

if [ ! -f ${HELPER_SCRIPT} ]; then
  echo "[`date`] The script ${HELPER_SCRIPT} is not available"
  return 0
fi

. ${HELPER_SCRIPT}

if [ "`is_uvm_running`x" = "truex" ]; then
  echo "[`date`] The UVM running, inserting queueing hooks"
  uvm_iptables_rules
  
  ## Ignore any traffic that is on the utun interface
  #{IPTablesCommand} -t #{Chain::FirewallRules.table} -I #{Chain::FirewallRules} 1 -i ${TUN_DEV} -j RETURN
  
  #{BypassRules}
else
  echo "[`date`] The UVM is currently not running"
fi

return 0

EOF

    os["override_manager"].write_file( UvmSubscriptionFile, text, "\n" )    
  end

  def write_packet_filter_script
    text = header
    
    text += <<EOF
HELPER_SCRIPT="/usr/share/untangle-net-alpaca/scripts/uvm/iptables"

if [ ! -f ${HELPER_SCRIPT} ]; then
  echo "[`date`] The script ${HELPER_SCRIPT} is not available"
  return 0
fi

. ${HELPER_SCRIPT}

if [ "`is_uvm_running`x" = "truex" ]; then
  echo "[`date`] The UVM running, service rules."
  uvm_packet_filter_standard_internal
  uvm_packet_filter_secure_internal
  uvm_packet_filter_secure_external
  uvm_packet_filter_secure_public
else
  echo "[`date`] The UVM is currently not running"
fi

return 0
EOF

    os["override_manager"].write_file( UvmServicesFile, text, "\n" )    
  end

  def write_openvpn_script
    text = header
    ## REVIEW This presently doesn't mark openvpn traffic as local.
    ## REVIEW 0x80 is a magic number.
    text  += <<EOF
HELPER_SCRIPT="/usr/share/untangle-net-alpaca/scripts/uvm/iptables"

if [ ! -f ${HELPER_SCRIPT} ]; then
  echo "[`date`] The script ${HELPER_SCRIPT} is not available"
  return 0
fi

. ${HELPER_SCRIPT}

if [ "`is_uvm_running`x" != "truex" ]; then 
  echo "[`date`] The UVM running, not inserting rules for openvpn"
  return 0
fi

if [ "`pidof openvpn`x" = "x" ]; then
  echo "[`date`] OpenVPN is not running, not inserting rules for openvpn"
  return 0
fi    

## This is the mark rule
#{IPTablesCommand} #{Chain::MarkInterface.args} -i tun0 -j MARK --or-mark #{0x80}

## Function designed to insert the necessary filter rule to pass traffic from a
## a VPN interface.
insert_vpn_export()
{
  local t_network=$1
  local t_netmask=$2

  #{IPTablesCommand} #{Chain::FirewallRules.args} -i tun0 -d ${t_network}/${t_netmask} -j RETURN
}

## Now insert all exports
EXPORTS_FILE=`bunnicula_home`/conf/openvpn/packet-filter-rules

if [ -f ${EXPORTS_FILE} ]; then 
  . ${EXPORTS_FILE}
  ## At the end block everything else
  #{IPTablesCommand} #{Chain::FirewallRules.args} -i tun0 -j DROP
fi
EOF
    
    os["override_manager"].write_file( UvmOpenVPNFile, text, "\n" )
  end

  ## This writes a file that indicates to the UVM the order
  ## of the interfaces
  def write_interface_order
    ## Create an interface map
    interfaces = {}
    Interface.find( :all ).each { |interface| interfaces[interface.index] = interface }
    
    settings = UvmSettings.find( :first )
    settings = UvmSettings.new if settings.nil?
    
    intf_order = settings.interface_order
    intf_order = UvmHelper::DefaultOrder if ApplicationHelper.null?( intf_order )

    intf_order = intf_order.split( "," ).map { |idx| idx.to_i }.delete_if { |idx| idx == 0 }
    
    values = []
    ## Go through and delete the interfaces that are in the map.
    intf_order.each do |idx|
      next values << "VPN:tun0:#{idx}" if idx == UvmHelper::VpnIndex
      
      interface = interfaces[idx]
      next if interface.nil?
      
      ## Delete the item at index for the second loop
      interfaces.delete( idx )
      
      ## Append the index
      values << interface_property( interface )
    end

    ## Append the remaining values ordered by their index.
    values += interfaces.keys.sort.map { |k| interface_property( interfaces[k] ) }

    os["override_manager"].write_file( UvmInterfaceProperties, <<EOF )
# #{Time.new}
# Auto Generated by the Untangle Net Alpaca
# If you modify this file manually, your changes
# may be overriden
#{UvmInterfaceOrderProperty}=#{values.join( "," )}
EOF
  end

  def subscription_rules
    text = <<EOF
#{BypassRules}() {
  local t_rule
EOF

    ## Add the user rules
    rules = Subscription.find( :all, :conditions => [ "system_id IS NULL AND enabled='t'" ] )
    ## Add the system rules
    rules += Subscription.find( :all, :conditions => [ "system_id IS NOT NULL AND enabled='t'" ] )
    
    rules.each do |rule|
      begin
        next if rule.is_custom

        filters, chain = OSLibrary::Debian::Filter::Factory.instance.filter( rule.filter )
        
        target = ( rule.subscribe ) ? "-j RETURN" : "-g #{Chain::BypassMark}"

        filters.each do |filter|
          break if filter.strip.empty?
          text << "#{IPTablesCommand} #{Chain::BypassRules.args} #{filter} #{target}\n"
        end
        
      rescue
        logger.warn( "The filter '#{rule.filter}' could not be parsed: #{$!}" )
      end
    end
    
    text + "\n}\n"
  end

  ## Review: This should be a global function
  def header
    <<EOF
#!/bin/dash

## #{Time.new}
## Auto Generated by the Untangle Net Alpaca
## If you modify this file manually, your changes
## may be overriden

EOF
  end

  def interface_property( interface )
    os_name = interface.os_name

    os_name += interface.index.to_s unless interface.is_mapped?

    ## Always register the interface as ppp0 if this is for PPPoE
    os_name = "ppp0" if interface.config_type == InterfaceHelper::ConfigType::PPPOE

    "#{interface.name}:#{os_name}:#{interface.index}"
  end
end
