# Methods added to this helper will be available to all templates in the application.
module ApplicationHelper
  # return true if a field is nil or null.
  def ApplicationHelper.null?( field )
    return true if field.nil?
    field = field.strip
    return true if field.empty?
    return true if ( field.upcase == "NULL" )    
    false
  end

  def editable_table( options = {} )
    result = ""
    if ! options[:title].nil?
      result << "<h2>" + options[:title] + "</h2>"
    end

    tableId="e_table_#{rand( 0x100000000 )}"
    #result << link_to_remote( "+".t, :url => { :action => options[:action].to_s } )

    blank_columns = ""
    options[:column_names].each do |column|
      blank_columns << "<div class=\"list-table-column\">"
      blank_columns << "<input name=\""+column+"['+rowId+']\" type=\"text\" size=\"30\" />"
      blank_columns << "</div>"
    end

    result << javascript_tag("function addRow"+tableId+"() { var rowId=Math.floor(Math.random()*10000000000); new Insertion.Bottom('"+tableId+"','<li id=\"'+rowId+'\" class=\"list-table-row\"><input type=\"hidden\" name=\""+options[:rows_name]+"[]\" value=\"'+rowId+'\" />"+blank_columns+"<div class=\"minus\" onClick=\"Alpaca.removeStaticEntry(\\''+rowId+'\\')\"> - </div></li> '); resize"+tableId+"(); }")

    result << button_to_function("+".t, "addRow"+tableId+"()")
    result << "<div class=\"list-table " + options[:class].to_s + "\">"
    result << "<ul id=\"" + tableId + "\" class=\"list-table-list " + options[:class].to_s + "\">"
    result << "<li class=\"header " + options[:header_class].to_s + "\">"
    
    options[:header_columns].each do |column|
      result << "<div class=\"list-table-header-column " + column[:class].to_s + "\">"
      result << column[:name].to_s + "</div>"
    end

    result << "</li>\n"

    options[:rows].each do |row|
      rowId="e-row-#{rand( 0x100000000 )}"
      result << "<li id=\""+rowId+"\" class=\"list-table-row " + row[:class].to_s + "\">"
      result << hidden_field_tag( options[:rows_name] + "[]", rowId )
      
      column_count = 0
      row[:columns].each do |column|
        result << "<div class=\"list-table-column " + column[:class].to_s  + "\">"
        result << text_field( options[:column_names][column_count], rowId, { :value => column[:value] } )
        result << "</div>"
        column_count = column_count + 1
      end
      result << "<div class=\"minus\" onClick=\"Alpaca.removeStaticEntry( '" + rowId + "' )\"> - </div>"
      result << "</li>"
    end
    result << "</ul>\n</div>\n"

    auto_size = "function resize"+tableId+"() {"
    if options[:auto_size]
      auto_size << "var v"+tableId+" = document.getElementById('"+tableId+"'); var v"+tableId+"w= v"+tableId+".offsetWidth;  var c"+tableId+" = v"+tableId+".childNodes; for(var i = 0; i < c"+tableId+".length; i++){if (c"+tableId+"[i].nodeName.toLowerCase() == 'li') { var lic = c"+tableId+"[i].childNodes; for (var c = 0; c < lic.length; c++) { if (lic[c].nodeName.toLowerCase() == 'div') { lic[c].style.width = Math.floor((v"+tableId+"w) / "+options[:header_columns].length.to_s+")-1+'px';} } } }"
    end
    auto_size << "}"
    result << javascript_tag(auto_size)
    return result
  end
end
