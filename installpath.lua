-- Script to find the install path for a C module. Public domain.

if not arg or not arg[1] then
  io.write("Usage: lua installpath.lua modulename\n")
  os.exit(1)
end

for p in string.gmatch(arg[1]:find('.so',1,1) and package.cpath or package.path , "[^;]+") do
  if string.sub(p, 1, 1) ~= "." then
    io.write(p:gsub(arg[1]:find('.so',1,1) and '?.so' or '?.lua' ,''), "\n")
    return
  end
end
error("no suitable installation path found")
