-- Final Lap Mushroom: the player starts the last lap with a mushroom when
-- the item slot is empty, and every finish is announced on the HUD.

function on_lap(kart, lap)
  if not kart.player or lap ~= race.laps - 1 then return end
  if race.give_item(kart.index, "mushroom") then
    race.message("Final lap! Have a mushroom", 180)
    print("final lap mushroom: place " .. kart.place .. " tick " .. race.tick)
  else
    race.message("Final lap!", 120)
    print("final lap, slot busy with " .. tostring(kart.item))
  end
end

function on_finish(kart, place)
  if kart.player then
    race.message("Finished in place " .. place, 240)
  end
  print("finish: kart " .. kart.index .. " place " .. place .. " tick " .. race.tick)
end
