<h1>Heroes Ascent Client</h1>

Heroes Ascent Client is a Guild Wars 2 addon built for the Heroes Ascent Competition, organized by the Italian community L’Arco del Leone.
The client integrates directly with Guild Wars 2 through RTAPI and Mumble Link, monitoring real-time player data to ensure fair competition and automatic rule validation.

Features:
   
    Automatic rule enforcement for the Heroes Ascent event
    Live data transmission to the Heroes Ascent server
    Violation detection (food usage, healing skills, map restrictions, etc.)
    Player registration system with secure API key validation
    Server feedback display (status messages, colors, violations)
    Localization support (English / Italian)
    UI built with ImGui for in-game interaction

How It Works
    
    The client continuously reads RTAPI and Mumble data from the game:
        - Character name, map ID, position, profession, elite specialization, mount, etc.
        - Character state (alive, downed, gliding, underwater, etc.)
    Data is periodically sent to the Heroes Ascent Server through secure HTTP requests.
    The server validates the data and enforces all competition rules listed in the official document

Disclaimer

    Heroes Ascent Client is not affiliated with or endorsed by deltaconnected or arcdps
    Heroes Ascent Client is not affiliated with or endorsed by Arenanet, NCSoft or Guild Wars 2

    This addon, like arcdps, hooks network data. To do so it makes runtime modifications to the GW2 executable.
    Use at your own risk.

Dependencies

    ImgGui
    RTAPI
    Mumble
    ArcDps
    nlohmann/json
    C++17 or newer
