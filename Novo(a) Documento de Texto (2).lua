--[[
    Novo Sistema de Replicação Limpo
    
    Este script foi criado do zero, usando os princípios do Replicador anterior
    mas com uma estrutura mais simples e direta. Ele foca na captura e
    replicação de movimentos, sons e criação de objetos de forma segura,
    evitando qualquer função que possa ser interpretada como vulnerável.
    
    Características:
    - Interface de usuário (GUI) minimalista para controle.
    - Captura e reprodução de movimentos do jogador.
    - Captura e reprodução de sons.
    - Captura de criação de instâncias (sem replicação).
    - Totalmente seguro, sem "hooking" ou leitura/escrita de arquivos.
]]--

-- Obtém os serviços essenciais do jogo
local RunService = game:GetService("RunService")
local Players = game:GetService("Players")
local Workspace = game:GetService("Workspace")
local localPlayer = Players.LocalPlayer

-- Armazena os eventos gravados
local recordedEvents = {}
local isRecording = false
local isReplaying = false

-- Variáveis para rastreamento de estado
local lastPosition = nil
local lastCameraCFrame = nil

-- Função para criar a interface do usuário (GUI)
local function createGUI()
    local playerGui = localPlayer:WaitForChild("PlayerGui")
    local screenGui = Instance.new("ScreenGui")
    screenGui.Name = "CleanReplicatorGUI"
    screenGui.Parent = playerGui

    local mainFrame = Instance.new("Frame")
    mainFrame.Size = UDim2.new(0, 250, 0, 150)
    mainFrame.Position = UDim2.new(0, 10, 0, 10)
    mainFrame.BackgroundColor3 = Color3.new(0.1, 0.1, 0.1)
    mainFrame.BorderSizePixel = 2
    mainFrame.BorderColor3 = Color3.new(0, 1, 0)
    mainFrame.Parent = screenGui
    
    local title = Instance.new("TextLabel")
    title.Size = UDim2.new(1, 0, 0, 25)
    title.BackgroundTransparency = 1
    title.Text = "🎬 Replicador Limpo"
    title.TextColor3 = Color3.new(0, 1, 0)
    title.TextSize = 14
    title.Font = Enum.Font.SourceSansBold
    title.Parent = mainFrame
    
    local statusLabel = Instance.new("TextLabel")
    statusLabel.Name = "StatusLabel"
    statusLabel.Size = UDim2.new(1, 0, 0, 20)
    statusLabel.Position = UDim2.new(0, 0, 0, 30)
    statusLabel.BackgroundTransparency = 1
    statusLabel.TextColor3 = Color3.new(0.8, 0.8, 0.8)
    statusLabel.TextSize = 12
    statusLabel.TextXAlignment = Enum.TextXAlignment.Left
    statusLabel.Text = "Status: Parado"
    statusLabel.Parent = mainFrame

    -- Botões de controle
    local recordButton = Instance.new("TextButton")
    recordButton.Name = "RecordButton"
    recordButton.Size = UDim2.new(1, -10, 0, 25)
    recordButton.Position = UDim2.new(0, 5, 0, 55)
    recordButton.BackgroundColor3 = Color3.new(0.2, 0.2, 0.2)
    recordButton.Text = "▶️ Iniciar Gravação"
    recordButton.TextColor3 = Color3.new(1, 1, 1)
    recordButton.Parent = mainFrame
    
    local stopButton = Instance.new("TextButton")
    stopButton.Name = "StopButton"
    stopButton.Size = UDim2.new(1, -10, 0, 25)
    stopButton.Position = UDim2.new(0, 5, 0, 85)
    stopButton.BackgroundColor3 = Color3.new(0.2, 0.2, 0.2)
    stopButton.Text = "⏹️ Parar"
    stopButton.TextColor3 = Color3.new(1, 1, 1)
    stopButton.Parent = mainFrame

    local replayButton = Instance.new("TextButton")
    replayButton.Name = "ReplayButton"
    replayButton.Size = UDim2.new(1, -10, 0, 25)
    replayButton.Position = UDim2.new(0, 5, 0, 115)
    replayButton.BackgroundColor3 = Color3.new(0.2, 0.2, 0.2)
    replayButton.Text = "🔄 Iniciar Replicação"
    replayButton.TextColor3 = Color3.new(1, 1, 1)
    replayButton.Parent = mainFrame
    
    return mainFrame, statusLabel
end

-- Função para capturar eventos e salvá-los
local function captureEvent(eventType, data)
    if not isRecording then return end
    
    local event = {
        type = eventType,
        data = data,
        timestamp = os.time()
    }
    table.insert(recordedEvents, event)
    print("Evento capturado: " .. eventType)
end

-- Lógica de captura principal
local function startCapture()
    print("▶️ Captura iniciada.")
    lastPosition = (localPlayer.Character and localPlayer.Character:FindFirstChild("HumanoidRootPart")) and localPlayer.Character.HumanoidRootPart.Position or nil
    lastCameraCFrame = Workspace.CurrentCamera.CFrame
    
    -- Captura de movimento e câmera
    RunService.Heartbeat:Connect(function()
        if not isRecording then return end
        
        local currentPosition = (localPlayer.Character and localPlayer.Character:FindFirstChild("HumanoidRootPart")) and localPlayer.Character.HumanoidRootPart.Position or nil
        if currentPosition and lastPosition and (currentPosition - lastPosition).Magnitude > 0.1 then
            captureEvent("Movement", { Position = currentPosition })
            lastPosition = currentPosition
        end
        
        local currentCameraCFrame = Workspace.CurrentCamera.CFrame
        if currentCameraCFrame ~= lastCameraCFrame then
            captureEvent("Camera", { CFrame = currentCameraCFrame })
            lastCameraCFrame = currentCameraCFrame
        end
    end)
    
    -- Captura de sons
    for _, sound in ipairs(Workspace:GetDescendants()) do
        if sound:IsA("Sound") then
            sound.Played:Connect(function()
                captureEvent("Sound", { SoundId = sound.SoundId, Volume = sound.Volume })
            end)
        end
    end
    Workspace.DescendantAdded:Connect(function(descendant)
        if descendant:IsA("Sound") then
            descendant.Played:Connect(function()
                captureEvent("Sound", { SoundId = descendant.SoundId, Volume = descendant.Volume })
            end)
        end
    end)
    
    -- Captura de criação de instâncias
    Workspace.DescendantAdded:Connect(function(descendant)
        captureEvent("InstanceCreated", { Class = descendant.ClassName, Path = descendant:GetFullName() })
    end)
end

-- Lógica de replicação principal
local function startReplay()
    print("🔄 Replicação iniciada.")
    isReplaying = true
    
    -- Ordena os eventos por tempo
    table.sort(recordedEvents, function(a, b) return a.timestamp < b.timestamp end)
    
    local lastTimestamp = nil
    for _, event in ipairs(recordedEvents) do
        if not isReplaying then break end
        
        if lastTimestamp then
            wait(event.timestamp - lastTimestamp)
        end
        
        if event.type == "Movement" then
            local character = localPlayer.Character
            if character and character:FindFirstChild("HumanoidRootPart") then
                character.HumanoidRootPart.CFrame = CFrame.new(event.data.Position)
            end
        elseif event.type == "Camera" then
            Workspace.CurrentCamera.CFrame = event.data.CFrame
        elseif event.type == "Sound" then
            local sound = Instance.new("Sound")
            sound.SoundId = event.data.SoundId
            sound.Volume = event.data.Volume
            sound.Parent = Workspace
            sound:Play()
            sound.Ended:Connect(function() sound:Destroy() end)
        end
        
        lastTimestamp = event.timestamp
    end
    
    isReplaying = false
    print("✅ Replicação concluída.")
end

-- Função principal de inicialização
local function main()
    -- Limpa GUIs antigas
    for _, child in ipairs(localPlayer.PlayerGui:GetChildren()) do
        if child.Name == "CleanReplicatorGUI" then
            child:Destroy()
        end
    end

    local guiFrame, statusLabel = createGUI()
    
    guiFrame.RecordButton.MouseButton1Click:Connect(function()
        isRecording = true
        isReplaying = false
        recordedEvents = {}
        statusLabel.Text = "Status: Gravando..."
    end)
    
    guiFrame.StopButton.MouseButton1Click:Connect(function()
        isRecording = false
        isReplaying = false
        statusLabel.Text = "Status: Parado"
        print("⏹️ Gravação parada. Eventos salvos: " .. #recordedEvents)
    end)
    
    guiFrame.ReplayButton.MouseButton1Click:Connect(function()
        if #recordedEvents > 0 then
            isRecording = false
            startReplay()
            statusLabel.Text = "Status: Replicando..."
        else
            warn("⚠️ Nenhum evento gravado para replicar.")
        end
    end)
    
    startCapture()
end

-- Inicia o script
main()
