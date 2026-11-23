// Pages/MainPage.xaml.cs
using Microsoft.Maui.Controls;
using Microsoft.Maui.Graphics;
using System;
using System.Collections.Generic;

namespace Controll.Pages;

public partial class MainPage : ContentPage
{
    private bool   _isTimingVisible      = false;
    private bool   _manualOverride       = false;
    private string _currentTabKey        = "data";
    private bool   _isSwitchingInternally = false;

    private readonly Dictionary<string, ContentView> _pages = new();
    private readonly List<string> _navigationHistory        = new();

    // Timing példányt megőrizzük hogy sensor update működjön
    private Timing _timingPage;

    public MainPage()
    {
        InitializeComponent();

        BluetoothManager.Initialize(Plugin.BLE.CrossBluetoothLE.Current.Adapter);

        BluetoothManager.OnConnectionChanged += (isConnected) =>
            UpdateMainConnectionIndicator(isConnected);

        // Szenzor adatok továbbítása a Timing oldalnak
        BluetoothManager.OnDataReceived += OnGlobalDataReceived;

        UpdateMainConnectionIndicator(BluetoothManager.IsConnected);
        SetActiveTab("data");
        LoadContent("data");
    }

    // ── Szenzor adatok fogadása minden oldalhoz ──
    private void OnGlobalDataReceived(string data)
    {
        if (string.IsNullOrWhiteSpace(data)) return;
        if (!data.StartsWith("TMP:")) return;

        var parts = data.Substring(4).Split(':');
        if (parts.Length < 1) return;

        if (float.TryParse(parts[0].Trim().Replace(',', '.'),
                System.Globalization.NumberStyles.Any,
                System.Globalization.CultureInfo.InvariantCulture, out float ntc1))
        {
            float ntc2 = float.NaN;
            if (parts.Length >= 2)
                float.TryParse(parts[1].Trim().Replace(',', '.'),
                    System.Globalization.NumberStyles.Any,
                    System.Globalization.CultureInfo.InvariantCulture, out ntc2);

            MainThread.BeginInvokeOnMainThread(() =>
                _timingPage?.UpdateSensors(ntc1, ntc2));
        }
    }

    // ══════════════ TAB KEZELÉS ══════════════
    private void OnDataTabClicked(object s, EventArgs e)    => SwitchTab("data");
    private void OnControlTabClicked(object s, EventArgs e) => SwitchTab("controll");
    private void OnNoteTabClicked(object s, EventArgs e)    => SwitchTab("note");
    private void OnRgbTabClicked(object s, EventArgs e)     => SwitchTab("RGB");

    private void SwitchTab(string tabKey)
    {
        if (_currentTabKey != tabKey && !_isSwitchingInternally)
            _navigationHistory.Add(_currentTabKey);

        _isTimingVisible = false;
        _currentTabKey   = tabKey;
        SetActiveTab(tabKey);
        LoadContent(tabKey);
    }

    private void SetActiveTab(string activeTab)
    {
        ResetAllTabs();
        var activeColor = Color.FromArgb("#FF453A");
        switch (activeTab)
        {
            case "data":     DataTabLabel.TextColor    = activeColor; break;
            case "RGB":      RgbTabLabel.TextColor     = activeColor; break;
            case "controll": ControlTabLabel.TextColor = activeColor; break;
            case "note":     NoteTabLabel.TextColor    = activeColor; break;
        }
    }

    private void ResetAllTabs()
    {
        var white = Color.FromArgb("#FFFFFF");
        DataTabLabel.TextColor    = white;
        ControlTabLabel.TextColor = white;
        NoteTabLabel.TextColor    = white;
        RgbTabLabel.TextColor     = white;
    }

    // ══════════════ CONTENT BETÖLTÉS ══════════════
    private void LoadContent(string pageName)
    {
        // FONTOS: a Data oldal 1mp-es lekérdező timere (_queryTimer) eddig
        // SOHA nem állt le tab-váltáskor - a StopPage() definiálva volt, de
        // sehol nem hívtuk meg. Emiatt a "data" fül elhagyása után a timer
        // örökre a háttérben futott tovább, és minden másodpercben
        // GET_TMP/GET_CTR/GET_RGB írásokat küldött - ez ütközött össze a
        // Settings oldalon futó BLE OTA írásokkal ("prior command is not
        // finished"), sőt a stray szöveges parancsok az OTA bináris
        // adatfolyamba is belekeveredhettek, ami a flash-írást korrumpálta.
        if (ContentArea.Content is Data prevData)
            prevData.StopPage();

        if (pageName == "note")
        {
            ContentArea.Content = new Note();
        }
        else
        {
            if (!_pages.ContainsKey(pageName))
            {
                ContentView page = pageName switch
                {
                    "data"     => new Data(),
                    "RGB"      => new RGB(),
                    "controll" => new Ctrl(),
                    _          => new Data()
                };
                _pages[pageName] = page;
            }
            ContentArea.Content = _pages[pageName];
        }

        // Overlay
        NoConnectionOverlayMain.IsVisible =
            pageName != "note" && !_manualOverride && !BluetoothManager.IsConnected;

        // Oldal init
        if (ContentArea.Content is RGB    rgbPage)  rgbPage.InitializePage();
        if (ContentArea.Content is Ctrl   ctrlPage) ctrlPage.InitializePage();
        if (ContentArea.Content is Data   dataPage) dataPage.InitializePage();
    }

    // ══════════════ TIMING NAVIGÁCIÓ ══════════════
    public void ShowTiming()
    {
        if (_timingPage == null)
            _timingPage = new Timing();

        ContentArea.Content = _timingPage;
        _isTimingVisible    = true;

        // Betöltjük az ESP-ből az összes beállítást + az aktuális módot
        if (BluetoothManager.IsConnected)
        {
            _ = BluetoothManager.WriteAsync("GET_ALL");
            _ = BluetoothManager.WriteAsync("GET_STATE");
        }
    }

    public void ShowControlFromTiming()
    {
        _isTimingVisible = false;
        SwitchTab("controll");
    }

    // ══════════════ BT ÁLLAPOT ══════════════
    private void UpdateMainConnectionIndicator(bool isConnected)
    {
        MainThread.BeginInvokeOnMainThread(async () =>
        {
            ConnectionIndicatorMain.Fill = isConnected ? Colors.LimeGreen : Colors.Red;

            if (!isConnected && _currentTabKey != "note" && !_manualOverride)
                NoConnectionOverlayMain.IsVisible = true;
            else if (isConnected)
                NoConnectionOverlayMain.IsVisible = false;

            if (!isConnected && _currentTabKey != "note")
                await DisplayAlert("Kapcsolat", "Nincs aktív Bluetooth kapcsolat.", "OK");
        });
    }

    private void OnBrowseTapped(object s, EventArgs e)
    {
        _manualOverride = true;
        NoConnectionOverlayMain.IsVisible = false;
    }

    private async void OnConnectClicked(object s, EventArgs e)
        => await Shell.Current.GoToAsync("settings");

    private async void OnSettingsClicked(object s, EventArgs e)
        => await Shell.Current.GoToAsync("settings");

    protected override void OnAppearing()
    {
        base.OnAppearing();
        UpdateMainConnectionIndicator(BluetoothManager.IsConnected);
        if (!string.IsNullOrEmpty(_currentTabKey))
            LoadContent(_currentTabKey);
    }

    protected override void OnDisappearing()
    {
        base.OnDisappearing();
        // Amikor elnavigálunk a MainPage-ről (pl. Settings-re firmware
        // frissítéshez), a Data oldal háttérben futó lekérdező timere
        // enélkül tovább futna és ütközne az OTA BLE írásokkal.
        if (ContentArea.Content is Data activeData)
            activeData.StopPage();
    }
}
