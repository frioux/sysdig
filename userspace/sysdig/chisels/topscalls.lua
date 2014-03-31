-- The number of items to show
TOP_NUMBER = 30

-- Chisel description
description = "Show the top " .. TOP_NUMBER .. " system calls in terms of number of calls. You can use filters to restrict this to a specific process, thread or file."
short_description = "Top system calls by number of calls"
category = "Performance"

-- Chisel argument list
args = {}

-- Argument notification callback
function on_set_arg(name, val)
	return false
end

-- Initialization callback
function on_init()
	chisel.exec("table_generator", 
		"evt.type",
		"System Call",
		"evt.count",
		"# Calls",
		"evt.type!=switch",
		"" .. TOP_NUMBER,
		"none")
	return true
end