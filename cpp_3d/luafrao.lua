-- control_uhjk.lua (con rotacin desde Lua)
-- Movimiento con U=adelante, J=atrs, H=izquierda, K=derecha, ESPACIO=salto

local velocidad = 8.0
local factorShift = 2.5

-- Cdigos de teclas (Raylib): U=85, J=74, H=72, K=75, ESPACIO=32, SHIFT_IZQ=340, SHIFT_DER=344
local adelante  = IsKeyDown(74) and 1 or 0   -- U
local atras     = IsKeyDown(85) and 1 or 0   -- J
local izquierda = IsKeyDown(72) and 1 or 0   -- H
local derecha   = IsKeyDown(75) and 1 or 0   -- K
local saltar    = IsKeyDown(32) and 1 or 0   -- ESPACIO
local corriendo = (IsKeyDown(340) or IsKeyDown(344)) and 1 or 0

-- Direccin local (relativa a la cmara)
local dirX = (derecha - izquierda)
local dirZ = (adelante - atras)

-- Normalizar
local longitud = math.sqrt(dirX * dirX + dirZ * dirZ)
if longitud > 0 then
    dirX = dirX / longitud
    dirZ = dirZ / longitud
end

-- Velocidad final
local vel = velocidad
if corriendo == 1 then
    vel = velocidad * factorShift
end

-- Rotar segn el ngulo de la cmara (recibido del motor)
local ang = camAngle
moveX = (dirX * math.cos(ang) + dirZ * math.sin(ang)) * vel * dt
moveZ = (-dirX * math.sin(ang) + dirZ * math.cos(ang)) * vel * dt

-- Salto
if saltar == 1 and enElSuelo == 1 then
    jump = true
else
    jump = false
end