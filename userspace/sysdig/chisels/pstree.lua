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

function on_capture_end(ts_s, ts_ns, delta) -- {{{
   if islive then
      terminal.clearscreen()
      terminal.moveto(0 ,0)
      terminal.showcursor()
      return true
   end

   print_sorted_table(grtable, ts_s, 0, delta, vizinfo)

   return true
end -- }}}
-- Argument initialization Callback
function on_set_arg(name, val) -- {{{
   if name == "filter" then
      filter = val
      return true
   end

   return false
end -- }}}

-- Imports and globals
require "common"
local dctable = {}
local capturing = false
local filter = nil
local match = false

-- Argument notification callback
function on_set_arg(name, val) -- {{{
   if name == "filter" then
      filter = val
      return true
   end

   return false
end -- }}}

-- Initialization callback
function on_init() return true end

function on_capture_start() -- {{{
   capturing = true
   return true
end -- }}}

function generate_forest(processes) -- {{{
   local ret = {}
   for pid, proc in pairs(processes) do
      if not processes[proc.ptid] then
         ret[proc.tid] = generate_tree(processes, pid)
      end
   end
   return ret
end -- }}}

function generate_tree(processes, ppid) -- {{{
   local ret = {}
   for x, proc in pairs(processes) do
      if proc.ptid == ppid then
         ret[proc.tid] = generate_tree(processes, proc.tid)
      end
   end
   return ret
end -- }}}

-- Event parsing callback
function on_event() -- {{{
   render_all()
   -- terminal.clearscreen()
   -- terminal.hidecursor()
end -- }}}

function render_all() -- {{{
   local processes = sysdig.get_thread_table(filter)
   local tree = generate_forest(processes)
   -- print(DataDumper(tree))

   for pid, kids in pairs(tree) do
      local comm = processes[pid].comm
      io.write(comm)
      local l = hash_to_list(kids, processes)
      render_tree(l, x_char(" ", #comm) .. " │ ")
   end
end -- }}}

function render_tree(list, _prefix) -- {{{
   local last = #list

   for k, name in pairs(list) do
      local comm = name[0]
      local kids = name[1]
      local prefix = _prefix
      if last == k then
         new_prefix = string.gsub(prefix, " │ $", " └─")
             prefix = string.gsub(prefix, " │ $", "   ")
      else
         new_prefix = string.gsub(prefix, " │ $", " ├─")
      end
      if k == 1 then
         new_prefix = ""
      end
      if #kids == 0 then
         print(new_prefix .. comm)
      elseif #kids > 1 then
         io.write(new_prefix .. comm .. "─┬─")
         render_tree(kids, prefix .. x_char(" ", #comm) .. " │ ")
      else
         io.write(new_prefix .. comm .. "───")
         render_tree(kids, prefix .. x_char(" ", #comm) .. "   ")
      end
   end
end -- }}}


-- this both converts from a hash-like structure:
--    {
--       3 = {},
--       1 = {},
--       2 = {},
--    }
--
-- to an array (and thus ordered) structure:
--
--    {
--       1,  {},
--       2,  {},
--       3,  {},
--    }
--
-- because arrays are ordered, this implies a sorting operation
function hash_to_list(tree, all_processes) -- {{{
   local names = {}
   for k, v in pairs(tree) do
      local val = {}
      val[0] = all_processes[k].comm
      val[1] = hash_to_list(v, all_processes)
      table.insert(names, val)
   end
   table.sort(names, function(a, b) return a[0] < b[0] end)

   return names
end -- }}}

function x_char(char, count) -- {{{
   local str = ""
   for i = 1, count, 1 do
      str = str .. char
   end
   return str
end -- }}}

function hash_size(hash) -- {{{
   if hash == nil then
      return 0
   end
   local i = 0
   for k, v in pairs(hash) do
      i = i + 1
   end
   return i
end -- }}}

function keys(hash) -- {{{
   local ret = {}
   for k, _ in pairs(hash) do
      table.insert(ret, k)
   end
   return ret
end -- }}}

--[[ DataDumper.lua
Copyright (c) 2007 Olivetti-Engineering SA

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
]]

local dumplua_closure = [[
local closures = {}
local function closure(t) 
  closures[#closures+1] = t
  t[1] = assert(loadstring(t[1]))
  return t[1]
end

for _,t in pairs(closures) do
  for i = 2,#t do 
    debug.setupvalue(t[1], i-1, t[i]) 
  end 
end
]]

local lua_reserved_keywords = {
  'and', 'break', 'do', 'else', 'elseif', 'end', 'false', 'for', 
  'function', 'if', 'in', 'local', 'nil', 'not', 'or', 'repeat', 
  'return', 'then', 'true', 'until', 'while' }

local function keys(t)
  local res = {}
  local oktypes = { stringstring = true, numbernumber = true }
  local function cmpfct(a,b)
    if oktypes[type(a)..type(b)] then
      return a < b
    else
      return type(a) < type(b)
    end
  end
  for k in pairs(t) do
    res[#res+1] = k
  end
  table.sort(res, cmpfct)
  return res
end

local c_functions = {}
for _,lib in pairs{'_G', 'string', 'table', 'math', 
    'io', 'os', 'coroutine', 'package', 'debug'} do
  local t = _G[lib] or {}
  lib = lib .. "."
  if lib == "_G." then lib = "" end
  for k,v in pairs(t) do
    if type(v) == 'function' and not pcall(string.dump, v) then
      c_functions[v] = lib..k
    end
  end
end

function DataDumper(value, varname, fastmode, ident)
  local defined, dumplua = {}
  -- Local variables for speed optimization
  local string_format, type, string_dump, string_rep = 
        string.format, type, string.dump, string.rep
  local tostring, pairs, table_concat = 
        tostring, pairs, table.concat
  local keycache, strvalcache, out, closure_cnt = {}, {}, {}, 0
  setmetatable(strvalcache, {__index = function(t,value)
    local res = string_format('%q', value)
    t[value] = res
    return res
  end})
  local fcts = {
    string = function(value) return strvalcache[value] end,
    number = function(value) return value end,
    boolean = function(value) return tostring(value) end,
    ['nil'] = function(value) return 'nil' end,
    ['function'] = function(value) 
      return string_format("loadstring(%q)", string_dump(value)) 
    end,
    userdata = function() error("Cannot dump userdata") end,
    thread = function() error("Cannot dump threads") end,
  }
  local function test_defined(value, path)
    if defined[value] then
      if path:match("^getmetatable.*%)$") then
        out[#out+1] = string_format("s%s, %s)\n", path:sub(2,-2), defined[value])
      else
        out[#out+1] = path .. " = " .. defined[value] .. "\n"
      end
      return true
    end
    defined[value] = path
  end
  local function make_key(t, key)
    local s
    if type(key) == 'string' and key:match('^[_%a][_%w]*$') then
      s = key .. "="
    else
      s = "[" .. dumplua(key, 0) .. "]="
    end
    t[key] = s
    return s
  end
  for _,k in ipairs(lua_reserved_keywords) do
    keycache[k] = '["'..k..'"] = '
  end
  if fastmode then 
    fcts.table = function (value)
      -- Table value
      local numidx = 1
      out[#out+1] = "{"
      for key,val in pairs(value) do
        if key == numidx then
          numidx = numidx + 1
        else
          out[#out+1] = keycache[key]
        end
        local str = dumplua(val)
        out[#out+1] = str..","
      end
      if string.sub(out[#out], -1) == "," then
        out[#out] = string.sub(out[#out], 1, -2);
      end
      out[#out+1] = "}"
      return "" 
    end
  else 
    fcts.table = function (value, ident, path)
      if test_defined(value, path) then return "nil" end
      -- Table value
      local sep, str, numidx, totallen = " ", {}, 1, 0
      local meta, metastr = (debug or getfenv()).getmetatable(value)
      if meta then
        ident = ident + 1
        metastr = dumplua(meta, ident, "getmetatable("..path..")")
        totallen = totallen + #metastr + 16
      end
      for _,key in pairs(keys(value)) do
        local val = value[key]
        local s = ""
        local subpath = path
        if key == numidx then
          subpath = subpath .. "[" .. numidx .. "]"
          numidx = numidx + 1
        else
          s = keycache[key]
          if not s:match "^%[" then subpath = subpath .. "." end
          subpath = subpath .. s:gsub("%s*=%s*$","")
        end
        s = s .. dumplua(val, ident+1, subpath)
        str[#str+1] = s
        totallen = totallen + #s + 2
      end
      if totallen > 80 then
        sep = "\n" .. string_rep("  ", ident+1)
      end
      str = "{"..sep..table_concat(str, ","..sep).." "..sep:sub(1,-3).."}" 
      if meta then
        sep = sep:sub(1,-3)
        return "setmetatable("..sep..str..","..sep..metastr..sep:sub(1,-3)..")"
      end
      return str
    end
    fcts['function'] = function (value, ident, path)
      if test_defined(value, path) then return "nil" end
      if c_functions[value] then
        return c_functions[value]
      elseif debug == nil or debug.getupvalue(value, 1) == nil then
        return string_format("loadstring(%q)", string_dump(value))
      end
      closure_cnt = closure_cnt + 1
      local res = {string.dump(value)}
      for i = 1,math.huge do
        local name, v = debug.getupvalue(value,i)
        if name == nil then break end
        res[i+1] = v
      end
      return "closure " .. dumplua(res, ident, "closures["..closure_cnt.."]")
    end
  end
  function dumplua(value, ident, path)
    return fcts[type(value)](value, ident, path)
  end
  if varname == nil then
    varname = "return "
  elseif varname:match("^[%a_][%w_]*$") then
    varname = varname .. " = "
  end
  if fastmode then
    setmetatable(keycache, {__index = make_key })
    out[1] = varname
    table.insert(out,dumplua(value, 0))
    return table.concat(out)
  else
    setmetatable(keycache, {__index = make_key })
    local items = {}
    for i=1,10 do items[i] = '' end
    items[3] = dumplua(value, ident or 0, "t")
    if closure_cnt > 0 then
      items[1], items[6] = dumplua_closure:match("(.*\n)\n(.*)")
      out[#out+1] = ""
    end
    if #out > 0 then
      items[2], items[4] = "local t = ", "\n"
      items[5] = table.concat(out)
      items[7] = varname .. "t"
    else
      items[2] = varname
    end
    return table.concat(items)
  end
end

-- vim: fdm=marker
