-- Chisel description
description = "List the running processes, with an output that is similar to the one of ps. Output is at a point in time; adjust this in the filter. It defaults to time of evt.num=0";
short_description = "List (and optionally filter) the machine processes.";
category = "System State";

-- Argument list
args =
{
   {
      name = "filter",
      description = "A sysdig-like filter expression that allows restricting the FD list. For example 'fd.name contains /etc' shows all the processes that have files open under /etc.",
      argtype = "filter",
      optional = true
   }
}

terminal = require "ansiterminal"

function on_capture_end(ts_s, ts_ns, delta)
	if islive then
		terminal.clearscreen()
		terminal.moveto(0 ,0)
		terminal.showcursor()
		return true
	end
	
	print_sorted_table(grtable, ts_s, 0, delta, vizinfo)
	
	return true
end
-- Argument initialization Callback
function on_set_arg(name, val)
   if name == "filter" then
      filter = val
      return true
   end

   return false
end

-- Imports and globals
require "common"
local dctable = {}
local capturing = false
local filter = nil
local match = false

-- Argument notification callback
function on_set_arg(name, val)
   if name == "filter" then
      filter = val
      return true
   end

   return false
end

-- Initialization callback
function on_init()
   return true
end

function on_capture_start()
   capturing = true
   return true
end

function generate_forest(processes)
   local ret = {}
   for pid, proc in pairs(processes) do
      if not processes[proc.ptid] then
         ret[proc.tid] = generate_tree(processes, pid)
      end
   end
   return ret
end

function generate_tree(processes, ppid)
   local ret = {}
   for x, proc in pairs(processes) do
      if proc.ptid == ppid then
         ret[proc.tid] = generate_tree(processes, proc.tid)
      end
   end
   return ret
end

-- Event parsing callback
function on_event()
   render_all()
   -- terminal.clearscreen()
   -- terminal.hidecursor()
end

function render_all()
   local processes = sysdig.get_thread_table(filter)
   local tree = generate_forest(processes)

   for pid, kids in pairs(tree) do
      local comm = processes[pid].comm
      io.write(comm)
      render_tree(processes, kids, pid, x_char(" ", #comm) .. " │ ")
   end
end

function render_tree(processes, pkids, ppid, _prefix)
   local i = 0
   local last = hash_size(pkids) - 1
   for pid, kids in pairs(pkids) do
      local prefix = _prefix
      local proc = processes[pid]
      local comm = proc.comm
      if last == i then
         new_prefix = string.gsub(prefix, " │ $", " └─")
             prefix = string.gsub(prefix, " │ $", "   ")
      else
         new_prefix = string.gsub(prefix, " │ $", " ├─")
      end
      if i == 0 then
         new_prefix = ""
      end
      if hash_size(kids) == 0 then
         print(new_prefix .. comm)
      elseif hash_size(kids) > 1 then
         io.write(new_prefix .. comm .. "─┬─")
         render_tree(processes, kids, pid, prefix .. x_char(" ", #comm) .. " │ ")
      else
         io.write(new_prefix .. comm .. "───")
         render_tree(processes, kids, pid, prefix .. x_char(" ", #comm) .. "   ")
      end
      i = i + 1
   end
end

function x_char(char, count)
   local str = ""
   for i = 1, count, 1 do
      str = str .. char
   end
   return str
end

function hash_size(hash)
   if hash == nil then
      return 0
   end
   local i = 0
   for k, v in pairs(hash) do
      i = i + 1
   end
   return i
end
