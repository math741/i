--[[
    GUI e Script de Sorte (LocalScript)

    Este script de cliente cria uma GUI funcional e um sistema
    simples de sorte que pode ser ativado e desativado pelo jogador.
    A sorte máxima agora se manifesta como um efeito de explosão de partes
    e uma aura de brilho no jogador.

    Coloque este script em StarterPlayerScripts.
]]--

-- Obtém os serviços essenciais do jogo
local Players = game:GetService("Players")
local Workspace = game:GetService("Workspace")
local RunService = game:GetService("RunService")
local Debris = game:GetService("Debris")

local localPlayer = Players.LocalPlayer
local isLuckActive = false
local currentMultiplier = 1
local luckEffect = nil -- Variável para a aura do jogador

-- ID de som para o efeito de sorte
local LUCK_SOUND_ID = "rbxassetid://131920677" -- Exemplo de som de brilho
local soundService = game:GetService("SoundService")

-- Função para criar a interface do usuário (GUI)
local function createGUI()
    -- Limpa GUIs antigas para evitar duplicatas
    for _, child in ipairs(localPlayer.PlayerGui:GetChildren()) do
        if child.Name == "LuckGUI" then
            child:Destroy()
        end
    end

    local screenGui = Instance.new("ScreenGui")
    screenGui.Name = "LuckGUI"
    screenGui.Parent = localPlayer.PlayerGui

    -- Cria o frame principal
    local mainFrame = Instance.new("Frame")
    mainFrame.Size = UDim2.new(0, 300, 0, 200)
    mainFrame.Position = UDim2.new(0.5, -150, 0.5, -100) -- Centraliza a GUI
    mainFrame.BackgroundColor3 = Color3.fromRGB(40, 40, 40)
    mainFrame.BorderColor3 = Color3.fromRGB(0, 200, 0)
    mainFrame.BorderSizePixel = 2
    mainFrame.Parent = screenGui

    -- Título
    local titleLabel = Instance.new("TextLabel")
    titleLabel.Size = UDim2.new(1, 0, 0, 30)
    titleLabel.Position = UDim2.new(0, 0, 0, 0)
    titleLabel.BackgroundColor3 = Color3.fromRGB(50, 50, 50)
    titleLabel.TextColor3 = Color3.fromRGB(255, 255, 255)
    titleLabel.Text = "🍀 Sistema de Sorte Máxima 🍀"
    titleLabel.Font = Enum.Font.SourceSansBold
    titleLabel.TextSize = 18
    titleLabel.Parent = mainFrame

    -- Rótulo do Multiplicador de Sorte
    local multiplierLabel = Instance.new("TextLabel")
    multiplierLabel.Name = "MultiplierLabel"
    multiplierLabel.Size = UDim2.new(1, 0, 0, 40)
    multiplierLabel.Position = UDim2.new(0, 0, 0, 40)
    multiplierLabel.BackgroundTransparency = 1
    multiplierLabel.TextColor3 = Color3.fromRGB(255, 255, 0)
    multiplierLabel.Text = "Multiplicador de Sorte: 1x"
    multiplierLabel.Font = Enum.Font.SourceSansBold
    multiplierLabel.TextSize = 24
    multiplierLabel.Parent = mainFrame
    
    -- Botão para ativar a sorte máxima
    local activateButton = Instance.new("TextButton")
    activateButton.Name = "ActivateButton"
    activateButton.Size = UDim2.new(1, -20, 0, 40)
    activateButton.Position = UDim2.new(0, 10, 0, 90)
    activateButton.BackgroundColor3 = Color3.fromRGB(0, 150, 0)
    activateButton.TextColor3 = Color3.fromRGB(255, 255, 255)
    activateButton.Text = "Ativar Sorte Máxima!"
    activateButton.Font = Enum.Font.SourceSansBold
    activateButton.TextSize = 16
    activateButton.Parent = mainFrame
    
    -- Botão para desativar
    local deactivateButton = Instance.new("TextButton")
    deactivateButton.Name = "DeactivateButton"
    deactivateButton.Size = UDim2.new(1, -20, 0, 40)
    deactivateButton.Position = UDim2.new(0, 10, 0, 140)
    deactivateButton.BackgroundColor3 = Color3.fromRGB(150, 0, 0)
    deactivateButton.TextColor3 = Color3.fromRGB(255, 255, 255)
    deactivateButton.Text = "Desativar Sorte"
    deactivateButton.Font = Enum.Font.SourceSansBold
    deactivateButton.TextSize = 16
    deactivateButton.Parent = mainFrame
    deactivateButton.Visible = false -- Oculta inicialmente

    return mainFrame, multiplierLabel, activateButton, deactivateButton
end

-- Lógica para o evento de chuva de sorte
local function startLuckyRain()
    if isLuckActive then return end

    isLuckActive = true
    print("Sorte máxima ativada!")

    -- Toca um som de ativação
    local sound = Instance.new("Sound")
    sound.SoundId = LUCK_SOUND_ID
    sound.Parent = soundService
    sound:Play()
    Debris:AddItem(sound, 5)

    -- Cria uma aura de partículas no jogador
    local character = localPlayer.Character or localPlayer.CharacterAdded:Wait()
    if character and character:FindFirstChild("HumanoidRootPart") then
        luckEffect = Instance.new("ParticleEmitter")
        luckEffect.Texture = "rbxassetid://135402035" -- Exemplo de textura de brilho
        luckEffect.Color = ColorSequence.new(Color3.fromRGB(255, 255, 0), Color3.fromRGB(255, 165, 0))
        luckEffect.Size = NumberSequence.new(0.5, 1.5)
        luckEffect.Transparency = NumberSequence.new(0, 1)
        luckEffect.Speed = NumberSequence.new(1, 3)
        luckEffect.Lifetime = 1
        luckEffect.Rate = 50 -- Emite 50 partículas por segundo
        luckEffect.Parent = character.HumanoidRootPart
    end

    local function spawnLuckyPart()
        if not isLuckActive or not localPlayer.Character then return end
        
        local character = localPlayer.Character
        local rootPart = character:FindFirstChild("HumanoidRootPart")
        if not rootPart then return end

        local part = Instance.new("Part")
        part.Size = Vector3.new(1, 1, 1)
        part.Position = rootPart.Position
        part.Anchored = false
        part.CanCollide = false
        part.Material = Enum.Material.Neon
        part.Color = Color3.fromRGB(math.random(0, 255), math.random(0, 255), math.random(0, 255))
        part.Parent = Workspace

        -- Aplica um impulso para simular uma explosão
        local impulseVector = Vector3.new(math.random(-1, 1), math.random(2, 4), math.random(-1, 1)) * 50
        part.CFrame = CFrame.new(part.Position) * CFrame.Angles(math.random(1, 10), math.random(1, 10), math.random(1, 10))
        part:ApplyImpulse(impulseVector)

        -- Adiciona um efeito de luz e brilho
        local pointLight = Instance.new("PointLight")
        pointLight.Color = part.Color
        pointLight.Range = 10
        pointLight.Parent = part
        
        -- Destrói a parte após 5 segundos
        Debris:AddItem(part, 5)
    end
    
    -- O loop de sorte continua até a variável isLuckActive ser desativada
    while isLuckActive do
        spawnLuckyPart()
        wait(0.1) -- Cria uma nova parte a cada 0.1 segundos
    end
end

-- Função para parar o efeito de sorte
local function stopLuckyRain()
    isLuckActive = false
    print("Sorte desativada.")
    
    -- Destrói a aura de partículas do jogador
    if luckEffect and luckEffect.Parent then
        luckEffect:Destroy()
    end
end

-- Função para atualizar o rótulo do multiplicador
local function updateMultiplierLabel(multiplierLabel, multiplier)
    multiplierLabel.Text = "Multiplicador de Sorte: " .. tostring(multiplier) .. "x"
end

-- Conecta a lógica aos botões
local function main()
    local guiFrame, multiplierLabel, activateButton, deactivateButton = createGUI()
    
    -- Conecta o botão de ativar
    activateButton.MouseButton1Click:Connect(function()
        if not isLuckActive then
            currentMultiplier = 100 -- Define a sorte para o máximo
            updateMultiplierLabel(multiplierLabel, currentMultiplier)
            startLuckyRain()
            activateButton.Visible = false
            deactivateButton.Visible = true
        end
    end)

    -- Conecta o botão de desativar
    deactivateButton.MouseButton1Click:Connect(function()
        if isLuckActive then
            currentMultiplier = 1
            updateMultiplierLabel(multiplierLabel, currentMultiplier)
            stopLuckyRain()
            activateButton.Visible = true
            deactivateButton.Visible = false
        end
    end)
end

-- Inicia o script
main()
