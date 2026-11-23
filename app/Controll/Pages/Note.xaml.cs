using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Text.Json;

namespace Controll.Pages;

public partial class Note : ContentView
{
    private ObservableCollection<AppLogEntry> _logEntries = new();
    private readonly string _filePath = Path.Combine(FileSystem.AppDataDirectory, "notes_log.json");
    public static Note CurrentInstance { get; private set; }

    public Note()
    {
        InitializeComponent();
        CurrentInstance = this;
        LogCollectionView.ItemsSource = _logEntries;
        LoadDataFromFile();
    }

    public void LoadDataFromFile()
    {
        try
        {
            if (!File.Exists(_filePath)) return;
            string json = File.ReadAllText(_filePath);
            var list = JsonSerializer.Deserialize<List<AppLogEntry>>(json);
            _logEntries.Clear();
            if (list != null)
            {
                foreach (var item in list) _logEntries.Add(item);
            }
        }
        catch (Exception ex) { Debug.WriteLine($"Hiba: {ex.Message}"); }
    }

    public void SaveDataToFile()
    {
        try
        {
            string json = JsonSerializer.Serialize(_logEntries, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(_filePath, json);
        }
        catch (Exception ex) { Debug.WriteLine($"Hiba: {ex.Message}"); }
    }

    private async void OnRowTapped(object sender, TappedEventArgs e)
    {
        if (e.Parameter is AppLogEntry entry)
        {
            await Application.Current.MainPage.Navigation.PushAsync(new NoteEditorPage(entry));
        }
    }

    private async void OnAddNewRowClicked(object sender, EventArgs e)
    {
        var newEntry = new AppLogEntry
        {
            Title = "",
            Description = "",
            Category = "",
            Priority = "Közepes",
            Date = DateTime.Now.ToString("yyyy.MM.dd HH:mm"),
            Deadline = DateTime.Now.ToString("yyyy.MM.dd")
        };

        _logEntries.Insert(0, newEntry);
        await Application.Current.MainPage.Navigation.PushAsync(new NoteEditorPage(newEntry));
    }

    private void OnDeleteRowClicked(object sender, EventArgs e)
    {
        if (sender is Button button && button.CommandParameter is AppLogEntry entry)
        {
            _logEntries.Remove(entry);
            SaveDataToFile();
        }
    }
}

public class AppLogEntry : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler PropertyChanged;
    protected void OnPropertyChanged([CallerMemberName] string name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    private string title = "";
    public string Title { get => title; set { title = value; OnPropertyChanged(); OnPropertyChanged(nameof(ShortTitle)); } }

    private string description = "";
    public string Description { get => description; set { description = value; OnPropertyChanged(); OnPropertyChanged(nameof(ShortDescription)); } }

    private string category = "";
    public string Category { get => category; set { category = value; OnPropertyChanged(); } }

    private string priority = "Közepes";
    public string Priority { get => priority; set { priority = value; OnPropertyChanged(); } }

    private string deadline = "";
    public string Deadline { get => deadline; set { deadline = value; OnPropertyChanged(); } }

    private string date = "";
    public string Date { get => date; set { date = value; OnPropertyChanged(); } }

    public string ShortTitle => string.IsNullOrEmpty(Title) ? "Névtelen" : (Title.Length > 32 ? Title.Substring(0, 32) + "..." : Title);
    public string ShortDescription => string.IsNullOrEmpty(Description) ? "Nincs leírás" : (Description.Length > 40 ? Description.Substring(0, 40) + "..." : Description);
}