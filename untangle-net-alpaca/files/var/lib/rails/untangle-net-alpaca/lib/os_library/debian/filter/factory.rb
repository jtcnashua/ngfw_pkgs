class OSLibrary::Debian::Filter::Factory
  include OSLibrary::Debian::Filter

  include Singleton

  def initialize
    @handlers = {}

    [ IPHandler, PortHandler, IntfHandler, LocalHandler, ProtocolHandler ].each do |handler|
      handler = handler.instance
      handler.parameters.each { |t| @handlers[t] = handler }
    end
  end
  
  ## Given a set of rules, this will create the corresponding filter string
  ## Returns, the table to add the rule to and the string for the iptable rules
  def filter( rules )
    parameters = {}

    ## Remove all of spaces
    rules = rules.gsub( " ", "" );
    
    ## Limit the parameters to only use each filter once.
    rules.split( RuleHelper::Separator ).each do |f|
      parameter, value = f.split( RuleHelper::TypeSeparator )
      ## Skip to the next one if either of the fields are nil
      next if ApplicationHelper.null?( parameter ) || ApplicationHelper.null?( value )
      
      ## Only use the first type for each argument.
      next unless parameters[parameter].nil?

      parameters[parameter] = value
    end

    filters = {}
    
    parameters.each do |parameter,value|
      handler = @handlers[parameter]
      next if handler.nil?
      
      handler.handle( parameter, value.strip, filters )
    end

    ## This indicates which chain to insert the rule into.
    chain = filters.delete( "chain" )

    ## Convert from a hash to an ordered array
    filters = filters.to_a.sort do |a,b| 
      next 0 if a[0] == b[0]
      ## Protocol must always go first
      next -1 if a[0] == "protocol"
      ## Protocol must always go first
      next 1 if b[0] == "protocol"
      
      ## For now, just put the remaining ones in alphabetical order
      a[0] <=> b[0]
    end
    
    r = [ "" ]
    ## Expand all of the combinations of all of the rules.
    filters.each do |parameter,value|
      n = r.map  do |s|
        next "#{s} #{value}" unless value.is_a?( Array )
        value.map { |v|  "#{s} #{v}" }
      end
      
      r = n.flatten
    end

    [ r, chain ]
  end

  def handle_day( value )
    raise "unsupported"
  end

  def handle_time( value )
    raise "unsupported"
  end
end
