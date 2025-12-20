# ZuesEngine Project Tracker

A comprehensive task management and documentation wiki for the ZuesEngine project.

## Features

- **Dashboard** - Overview of project progress with statistics
- **Todo List** - Track all tasks with filtering and priorities
- **Wiki/Docs** - In-depth documentation for developers
- **Roadmap** - Visual timeline of development phases

## Getting Started

### Opening the Tracker

Simply open `index.html` in any modern web browser:

```bash
# From the ProjectTracker directory
start index.html        # Windows
open index.html         # macOS
xdg-open index.html     # Linux
```

Or just double-click `index.html` in your file explorer.

### No Server Required

The tracker runs entirely in your browser using localStorage for persistence. No backend server needed!

## Usage Guide

### Managing Tasks

**Create Task:**
1. Click the "+ New Task" button
2. Fill in task details
3. Add subtasks if needed
4. Click "Save Task"

**Edit Task:**
- Click on any task title to edit it

**Filter Tasks:**
- Use category buttons in the sidebar
- Use status/priority filters in Todo List view

**Mark Complete:**
- Check the checkbox next to a task

### Viewing Documentation

1. Click "Wiki / Docs" in the sidebar
2. Browse topics in the left panel
3. Use search to find specific information

### Data Management

**Export Data:**
- Click "Export Data" in the sidebar
- Downloads a `.json` file with all tasks and wiki pages
- Useful for backups or sharing

**Import Data:**
- Click "Import Data" in the sidebar
- Select a previously exported `.json` file
- Confirms before replacing current data

## Git Integration

### Tracking Changes

The tracker data is stored in two ways:

1. **Browser localStorage** - Working data while you use the app
2. **JSON exports** - Backup/versioned data

### Recommended Workflow

1. Work in the tracker app (auto-saves to localStorage)
2. Periodically export to `tracker-backup-YYYY-MM-DD.json`
3. Commit exported JSON files to git
4. Team members can import the latest export

### Git-Friendly

- All data in human-readable JSON
- Easy to diff and merge
- No database required
- Portable across machines

## Data Storage

Data is stored in your browser's localStorage at:
- Key: `zeusEngineData`
- Format: JSON

### Clearing Data

To start fresh, open browser console (F12) and run:
```javascript
localStorage.removeItem('zeusEngineData');
location.reload();
```

## Initial Data

The tracker comes pre-loaded with:
- **Analysis of existing features** with detailed documentation
- **Todo items for missing features** with implementation guides
- **Wiki pages** covering all major systems

This initial data is in `data.js` and `data-continued.js`.

## Customization

### Adding More Wiki Pages

Edit `data-continued.js` and add to `window.initialData.wikiPages`:

```javascript
{
    id: "my-topic",
    title: "My Topic",
    content: `
        <h1>My Topic</h1>
        <p>Content here...</p>
    `
}
```

### Modifying Categories

Edit the categories in `index.html` (sidebar section) and `app.js` (task form).

## Browser Compatibility

Works in all modern browsers:
- Chrome/Edge (recommended)
- Firefox
- Safari
- Opera

Requires JavaScript enabled.

## Troubleshooting

**Data not saving:**
- Check that localStorage is enabled in your browser
- Check that you're not in private/incognito mode

**Tasks disappeared:**
- Check if you're in the right category filter
- Try exporting and re-importing your last backup

**Wiki not loading:**
- Hard refresh: Ctrl+F5 (Windows) or Cmd+Shift+R (Mac)
- Clear browser cache

## Development

The tracker is built with vanilla HTML/CSS/JavaScript:
- `index.html` - Main structure
- `styles.css` - All styling
- `app.js` - Application logic
- `data.js` + `data-continued.js` - Initial data

To modify, just edit the files and refresh your browser.

## License

Same as ZuesEngine - see main project LICENSE.md

## Questions?

For questions about:
- **The tracker app itself** - check this README
- **ZuesEngine development** - check the Wiki in the tracker
- **Contributing** - see the "Contributing Guide" in the Wiki
