-- ============================================================
-- DEMOSTRACIÓN COMPLETA DE SCRIPTING PARA EL MOTOR
-- ============================================================
-- Este script crea varios bloques con diferentes movimientos y colores
-- usando la sintaxis correcta del motor (GetTime() para animaciones).
-- ============================================================

-- ============================================================
-- 1. INICIALIZACIÓN
-- ============================================================
if GetBlockCount() == 0 then
    LogMessage("=== DEMOSTRACIÓN: INICIO ===")
    
    -- ============================================================
    -- 2. CREACIÓN DE BLOQUES CON DIFERENTES COLORES
    -- ============================================================
    -- CreateBlock(x, y, z, ancho, alto, largo, r, g, b)
    -- Los índices de bloque son 1-based (el primero es 1)
    
    -- Bloque 1: Rojo (se moverá en X)
    CreateBlock(-3, 1, 0, 2, 1, 2, 255, 0, 0)
    LogMessage("Bloque 1 (Rojo) creado en (-3, 1, 0)")
    
    -- Bloque 2: Verde (se moverá en Z)
    CreateBlock(3, 1, 0, 2, 1, 2, 0, 255, 0)
    LogMessage("Bloque 2 (Verde) creado en (3, 1, 0)")
    
    -- Bloque 3: Azul (se moverá en Y)
    CreateBlock(0, 1, -3, 2, 1, 2, 0, 0, 255)
    LogMessage("Bloque 3 (Azul) creado en (0, 1, -3)")
    
    -- Bloque 4: Amarillo (movimiento circular en XZ)
    CreateBlock(0, 1, 3, 2, 1, 2, 255, 255, 0)
    LogMessage("Bloque 4 (Amarillo) creado en (0, 1, 3)")
    
    -- Bloque 5: Magenta (movimiento elíptico)
    CreateBlock(-5, 2, -5, 2, 1, 2, 255, 0, 255)
    LogMessage("Bloque 5 (Magenta) creado en (-5, 2, -5)")
    
    -- Bloque 6: Cian (plataforma grande)
    CreateBlock(0, 0.5, 0, 8, 0.5, 4, 0, 255, 255)
    LogMessage("Bloque 6 (Cian - Plataforma) creado en (0, 0.5, 0)")
    
    LogMessage("=== DEMOSTRACIÓN: BLOQUES CREADOS ===")
    LogMessage("Total de bloques: " .. GetBlockCount())
    LogMessage("=== DEMOSTRACIÓN: FIN INICIALIZACIÓN ===")
end

-- ============================================================
-- 3. MOVIMIENTO CONTINUO (se ejecuta cada frame)
-- ============================================================
-- Obtener el tiempo global (GetTime() devuelve segundos desde inicio)
local tiempo = GetTime()

-- ============================================================
-- 3.1 Bloque 1 (Rojo): Oscila en X
-- ============================================================
local pos1 = GetBlockPos(1)
if pos1 then
    local x1 = -3 + 4 * math.sin(tiempo * 0.8)  -- oscila entre -7 y 1
    MoveBlock(1, x1, pos1.y, pos1.z)
end

-- ============================================================
-- 3.2 Bloque 2 (Verde): Oscila en Z
-- ============================================================
local pos2 = GetBlockPos(2)
if pos2 then
    local z2 = 4 * math.sin(tiempo * 0.6)  -- oscila entre -4 y 4
    MoveBlock(2, pos2.x, pos2.y, z2)
end

-- ============================================================
-- 3.3 Bloque 3 (Azul): Sube y baja en Y
-- ============================================================
local pos3 = GetBlockPos(3)
if pos3 then
    local y3 = 1 + 2 * math.sin(tiempo * 1.2)  -- oscila entre -1 y 3
    MoveBlock(3, pos3.x, y3, pos3.z)
end

-- ============================================================
-- 3.4 Bloque 4 (Amarillo): Movimiento circular en XZ
-- ============================================================
local pos4 = GetBlockPos(4)
if pos4 then
    local radio = 3.5
    local angulo = tiempo * 0.7
    local x4 = radio * math.cos(angulo)
    local z4 = radio * math.sin(angulo)
    MoveBlock(4, x4, pos4.y, z4)
end

-- ============================================================
-- 3.5 Bloque 5 (Magenta): Movimiento elíptico (X más amplio que Z)
-- ============================================================
local pos5 = GetBlockPos(5)
if pos5 then
    local angulo2 = tiempo * 0.5
    local x5 = 6 * math.cos(angulo2)
    local z5 = 3 * math.sin(angulo2)
    MoveBlock(5, x5, pos5.y, z5)
end

-- ============================================================
-- 3.6 Bloque 6 (Cian - Plataforma): Oscila en X lentamente
-- ============================================================
local pos6 = GetBlockPos(6)
if pos6 then
    local x6 = 4 * math.sin(tiempo * 0.3)  -- plataforma grande se mueve lento
    MoveBlock(6, x6, pos6.y, pos6.z)
end

-- ============================================================
-- 4. CAMBIO DE COLOR CÍCLICO (opcional)
-- ============================================================
-- Cambiar el color de cada bloque según el tiempo (efecto arcoíris)
-- Usamos valores RGB cíclicos

-- Bloque 1: Rojo → Verde → Azul
local r1 = math.floor((math.sin(tiempo * 0.5) + 1) * 127.5)
local g1 = math.floor((math.sin(tiempo * 0.5 + 2.094) + 1) * 127.5)
local b1 = math.floor((math.sin(tiempo * 0.5 + 4.188) + 1) * 127.5)
SetBlockColor(1, r1, g1, b1)

-- Bloque 2: Verde → Azul → Rojo (desfasado)
local r2 = math.floor((math.sin(tiempo * 0.5 + 1.047) + 1) * 127.5)
local g2 = math.floor((math.sin(tiempo * 0.5 + 3.141) + 1) * 127.5)
local b2 = math.floor((math.sin(tiempo * 0.5 + 5.236) + 1) * 127.5)
SetBlockColor(2, r2, g2, b2)

-- Bloque 3: Azul → Rojo → Verde (otro desfase)
local r3 = math.floor((math.sin(tiempo * 0.5 + 2.094) + 1) * 127.5)
local g3 = math.floor((math.sin(tiempo * 0.5 + 4.188) + 1) * 127.5)
local b3 = math.floor((math.sin(tiempo * 0.5 + 6.283) + 1) * 127.5)
SetBlockColor(3, r3, g3, b3)

-- Bloque 4: Color fijo (Amarillo) + variación sutil
local r4 = math.floor((math.sin(tiempo * 0.3) + 1) * 127.5)
local g4 = math.floor((math.sin(tiempo * 0.3 + 2.094) + 1) * 127.5)
local b4 = math.floor((math.sin(tiempo * 0.3 + 4.188) + 1) * 127.5)
SetBlockColor(4, r4, g4, b4)

-- Bloque 5: Magenta con variación
local r5 = math.floor((math.sin(tiempo * 0.7) + 1) * 127.5)
local g5 = math.floor((math.sin(tiempo * 0.7 + 1.047) + 1) * 127.5)
local b5 = math.floor((math.sin(tiempo * 0.7 + 2.094) + 1) * 127.5)
SetBlockColor(5, r5, g5, b5)

-- ============================================================
-- 5. LOGS DE DIAGNÓSTICO (cada 2 segundos)
-- ============================================================
if math.floor(tiempo) % 2 == 0 then
    LogMessage("=== ESTADO (t=" .. string.format("%.1f", tiempo) .. "s) ===")
    local count = GetBlockCount()
    LogMessage("Bloques activos: " .. count)
    
    -- Mostrar posición de algunos bloques
    local p1 = GetBlockPos(1)
    if p1 then LogMessage("Bloque1: X=" .. string.format("%.1f", p1.x)) end
    local p4 = GetBlockPos(4)
    if p4 then LogMessage("Bloque4: X=" .. string.format("%.1f", p4.x) .. " Z=" .. string.format("%.1f", p4.z)) end
    local p6 = GetBlockPos(6)
    if p6 then LogMessage("Plataforma: X=" .. string.format("%.1f", p6.x)) end
end

-- ============================================================
-- 6. MENSAJE FINAL (solo una vez)
-- ============================================================
if not fin then
    fin = true
    LogMessage("=== DEMOSTRACIÓN: SCRIPTS ACTIVOS ===")
    LogMessage("Los bloques se mueven y cambian de color.")
    LogMessage("Movimientos: X, Z, Y, circular, elíptico y plataforma.")
    LogMessage("=== DEMOSTRACIÓN: LISTA ===")
end