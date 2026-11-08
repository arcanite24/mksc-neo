-- Banana Swap: any kart's single green shell is thrown as a banana instead,
-- and spiny shells are refused (the kart keeps it in the slot). CPU karts go
-- through the same hook; in Time Trial it sees the player's mushrooms.

local swaps = 0

function on_item_use(kart, item)
  if item == "green_shell" then
    swaps = swaps + 1
    if kart.player then race.message("Shell swapped for a banana", 90) end
    print("swap " .. swaps .. ": kart " .. kart.index .. " green_shell -> banana at tick " .. race.tick)
    return "banana"
  elseif item == "spiny_shell" then
    if kart.player then race.message("No spiny shells here", 90) end
    print("kart " .. kart.index .. " cancelled spiny_shell at tick " .. race.tick)
    return false
  end
  print("kart " .. kart.index .. " used " .. item .. " at tick " .. race.tick)
end
