class OSLibrary::RoutesManager < Alpaca::OS::ManagerBase
  include Singleton

  ConfigFile = "/etc/untangle-net-alpacs/routes"



  def register_hooks
#    os["network_manager"].register_hook( -100, "routes_manager", "write_files", :hook_commit )
  end
  
  def hook_commit
    settings = DdclientSettings.find( :first )
    return if ( settings.nil? )
    cfg = []
    defaults = []
    
    if ( settings.enabled )
      conditions = [ "wan=?", true ]
      wanInterface = Interface.find( :first, :conditions => conditions )
      #logger.debug("settings.service is: " + settings.service)
      protocol = ConfigService[settings.service][0]
      server = ConfigService[settings.service][1]
      [ [ ConfigPid, DdclientPidFile ],
        [ ConfigUse, "if, if=" + wanInterface.os_name ],
        [ ConfigProtocol, protocol ],
        [ ConfigLogin, settings.login ],
        [ ConfigPassword, settings.password ],
        [ ConfigServer, server + '" "' +settings.hostname ]
      ].each do |var,val|
        next if ( val.nil? || val == "null" )
        cfg << "#{var}=\"#{val}\""
      end
    end
    
    
    #logger.debug( "running: " + DdclientCmdStop )
    Kernel.system( DdclientCmdStop  )
    os["override_manager"].write_file( ConfigFile, header, "\n", cfg.join( "\n" ), "\n" )
    if ( settings.enabled )
      #logger.debug( "running: " + DdclientRcd )
      Kernel.system( DdclientRcd  )
      #logger.debug( "running: " + DdclientCmdRestart )
      Kernel.system( DdclientCmdRestart  )
    end

    #Kernel.system( "hostname #{settings.hostname}" )
  end
  
  def header
    <<EOF
## #{Time.new}
## Auto Generated by the Untangle Net Alpaca
## If you modify this file manually, your changes
## may be overriden
EOF
  end
end
