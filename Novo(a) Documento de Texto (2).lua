--[[
    Script de Multiplicador de Sorte (ServerScript)
    
    Este script de servidor aumenta a sorte de todo o servidor em 4x.
    Ele demonstra um sistema simples que você pode adaptar para a sua
    necessidade, como aumentar a chance de um item raro ser gerado.
    
    Coloque este script em ServerScriptService.
]]--

-- Obtém os serviços essenciais do jogo
local Workspace = game:GetService("Workspace")
local Players = game:GetService("Players")

-- Define o multiplicador de sorte do servidor.
-- A sorte é um conceito programado, não um atributo nativo do Roblox.
-- Aqui, nós a representamos como um multiplicador.
local LUCK_MULTIPLIER = 4

-- Define a chance base de um evento "sortudo" acontecer (ex: 1 em 100)
local BASE_LUCKY_CHANCE = 100

-- Função para criar uma parte com uma cor aleatória
local function createRandomPart()
    -- Cria uma nova parte
    local newPart = Instance.new("Part")
    
    -- Define as propriedades básicas da parte
    newPart.Size = Vector3.new(2, 2, 2)
    newPart.Position = Vector3.new(math.random(-50, 50), 10, math.random(-50, 50))
    newPart.Anchored = true
    newPart.Parent = Workspace
    
    -- Gera um número aleatório de 1 a 100 para determinar a chance de sorte.
    local luckyRoll = math.random(1, BASE_LUCKY_CHANCE)
    
    -- Calcula a nova chance sortuda com o multiplicador.
    -- Por exemplo, se a chance base for 100, a nova chance será 100 / 4 = 25.
    -- Isso significa que você precisa tirar um número entre 1 e 25 para ser "sortudo".
    local newLuckyChance = BASE_LUCKY_CHANCE / LUCK_MULTIPLIER
    
    -- Verifica se o evento sortudo aconteceu.
    if luckyRoll <= newLuckyChance then
        -- Se o evento sortudo aconteceu, a cor da parte será dourada.
        newPart.Color = Color3.new(1, 0.843137, 0) -- Cor dourada
        print("🎉 Evento sortudo ativado! Uma parte dourada foi criada.")
    else
        -- Se não for sortudo, a cor será aleatória.
        newPart.Color = Color3.new(math.random(), math.random(), math.random())
    end
end

-- Loop principal do script
while true do
    -- Aguarda 5 segundos antes de executar a função novamente
    wait(5)
    
    -- Chama a função para criar uma nova parte
    createRandomPart()
end
