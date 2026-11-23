namespace Controll.Pages;

public partial class NoteEditorPage : ContentPage
{
    private AppLogEntry _entry;

    public NoteEditorPage(AppLogEntry entry)
    {
        InitializeComponent();
        _entry = entry;

        TitleEntry.Text = entry.Title;
        DescriptionEditor.Text = entry.Description;
        CategoryEntry.Text = entry.Category; // Picker helyett Entry szöveg
        PriorityPicker.SelectedItem = entry.Priority;

        if (DateTime.TryParse(entry.Deadline, out DateTime d))
            DeadlinePicker.Date = d;
    }

    private async void OnSaveClicked(object sender, EventArgs e)
    {
        _entry.Title = TitleEntry.Text;
        _entry.Description = DescriptionEditor.Text;
        _entry.Category = CategoryEntry.Text ?? ""; // Szöveges beviteli mezõ értéke
        _entry.Priority = PriorityPicker.SelectedItem?.ToString() ?? "Közepes";
        _entry.Deadline = DeadlinePicker.Date.ToString("yyyy.MM.dd");
        _entry.Date = DateTime.Now.ToString("yyyy.MM.dd HH:mm");

        Note.CurrentInstance?.SaveDataToFile();
        await Navigation.PopAsync();
    }
}