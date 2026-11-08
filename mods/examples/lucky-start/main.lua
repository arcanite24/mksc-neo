-- Lucky Start: a "get ready" banner during the countdown, then a random
-- starting item drawn from race.random, which is reproducible per race.
-- Time Trial keeps its three mushrooms, so there the draw only announces.

local prizes = { "mushroom", "banana", "green_shell", "triple_mushroom", "star" }
local announced = false
local item_users = {}

function on_tick(race)
  if race.phase == "countdown" and not announced then
    announced = true
    race.message("Get ready for a lucky start", 150)
  end
end

function on_race_start(race)
  local prize = prizes[race.random(#prizes)]
  local given = race.give_item(1, prize)
  if given then
    race.message("Lucky start: " .. prize:gsub("_", " "), 240)
  else
    race.message("No prize, drew " .. prize:gsub("_", " "), 240)
  end
  for index = 2, race.karts do
    if race.kart(index).player then
      local ok = race.give_item(index, "mushroom")
      print("local kart " .. index .. " mushroom " .. (ok and "given" or "not given"))
    end
  end
  print("race start (" .. race.mode .. ", course " .. race.course .. "): prize " ..
        prize .. (given and " given" or " not given"))
end

-- In local VS/Battle each human gets a prize through the same API. CPU
-- opponents stay untouched in single-player modes.
function on_item_use(kart, item)
  if kart.player then
    item_users[kart.index] = true
    print("local kart " .. kart.index .. " used " .. item)
  end
end

function on_race_end(race)
  local users = ""
  for index = 1, race.karts do
    if item_users[index] then users = users .. index end
  end
  print("local item users: " .. users)
end
