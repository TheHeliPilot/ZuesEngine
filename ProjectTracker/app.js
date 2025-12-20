// ZuesEngine Project Tracker v2 - Tree-based Task Management

class ProjectTracker {
    constructor() {
        this.tasks = [];
        this.wikiPages = [];
        this.currentView = 'tasks';
        this.editingTaskId = null;
        this.editingParentId = null;
        this.expandedTasks = new Set();
        this.searchQuery = '';

        this.init();
    }

    init() {
        this.loadData();
        this.initializeDefaultData();
        this.setupEventListeners();
        this.render();
    }

    // Generate unique ID
    generateId() {
        return Date.now().toString(36) + Math.random().toString(36).substr(2);
    }

    // Data persistence
    loadData() {
        try {
            const saved = localStorage.getItem('zuesTrackerV2');
            if (saved) {
                const data = JSON.parse(saved);
                this.tasks = data.tasks || [];
                this.wikiPages = data.wikiPages || [];
                this.expandedTasks = new Set(data.expandedTasks || []);
            }
        } catch (e) {
            console.error('Failed to load data:', e);
        }
    }

    saveData() {
        const data = {
            tasks: this.tasks,
            wikiPages: this.wikiPages,
            expandedTasks: Array.from(this.expandedTasks),
            version: '2.0'
        };
        localStorage.setItem('zuesTrackerV2', JSON.stringify(data));
    }

    initializeDefaultData() {
        if (window.defaultData) {
            // Always update wiki pages from default data (allows adding new docs)
            this.wikiPages = window.defaultData.wikiPages || [];

            // Only load tasks if none exist
            if (this.tasks.length === 0) {
                this.tasks = window.defaultData.tasks || [];
                // Expand root tasks by default
                this.tasks.forEach(t => this.expandedTasks.add(t.id));
            }
            this.saveData();
        }
    }

    // Event listeners
    setupEventListeners() {
        // Navigation
        document.querySelectorAll('.nav-item').forEach(btn => {
            btn.addEventListener('click', () => this.switchView(btn.dataset.view));
        });

        // Add root task
        document.getElementById('add-root-btn').addEventListener('click', () => {
            this.openModal(null, null);
        });

        // Collapse/Expand all
        document.getElementById('collapse-all-btn').addEventListener('click', () => {
            this.expandedTasks.clear();
            this.saveData();
            this.renderTasks();
        });

        document.getElementById('expand-all-btn').addEventListener('click', () => {
            this.expandAllTasks(this.tasks);
            this.saveData();
            this.renderTasks();
        });

        // Search
        document.getElementById('search-input').addEventListener('input', (e) => {
            this.searchQuery = e.target.value.toLowerCase();
            this.renderTasks();
        });

        // Modal
        document.getElementById('task-form').addEventListener('submit', (e) => {
            e.preventDefault();
            this.saveTask();
        });

        document.querySelectorAll('.modal-close').forEach(btn => {
            btn.addEventListener('click', () => this.closeModal());
        });

        document.getElementById('task-modal').addEventListener('click', (e) => {
            if (e.target.id === 'task-modal') this.closeModal();
        });

        // Export/Import/Reset/Reload
        document.getElementById('export-btn').addEventListener('click', () => this.exportData());
        document.getElementById('import-btn').addEventListener('click', () => this.importData());
        document.getElementById('reset-btn').addEventListener('click', () => this.resetData());
        document.getElementById('reload-defaults-btn').addEventListener('click', () => this.reloadDefaults());

        // Wiki search
        document.getElementById('wiki-search').addEventListener('input', (e) => {
            this.renderWiki(e.target.value.toLowerCase());
        });

        // Keyboard shortcuts
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') this.closeModal();
        });
    }

    expandAllTasks(tasks) {
        tasks.forEach(task => {
            if (task.children && task.children.length > 0) {
                this.expandedTasks.add(task.id);
                this.expandAllTasks(task.children);
            }
        });
    }

    // View switching
    switchView(view) {
        this.currentView = view;
        document.querySelectorAll('.nav-item').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.view === view);
        });
        document.querySelectorAll('.view').forEach(v => {
            v.classList.toggle('active', v.id === `${view}-view`);
        });
        this.render();
    }

    // Rendering
    render() {
        if (this.currentView === 'tasks') {
            this.renderTasks();
        } else {
            this.renderWiki();
        }
    }

    renderTasks() {
        const container = document.getElementById('task-tree');
        const stats = this.calculateStats(this.tasks);
        document.getElementById('stats-display').textContent =
            `${stats.completed}/${stats.total} completed (${stats.total > 0 ? Math.round(stats.completed/stats.total*100) : 0}%)`;

        if (this.tasks.length === 0) {
            container.innerHTML = '<p class="empty">No tasks yet. Click "+ Add Task" to create one.</p>';
            return;
        }

        const filteredTasks = this.searchQuery ? this.filterTasks(this.tasks) : this.tasks;

        if (filteredTasks.length === 0) {
            container.innerHTML = '<p class="empty">No tasks match your search.</p>';
            return;
        }

        container.innerHTML = this.renderTaskList(filteredTasks, true);
        this.attachTaskEventListeners();
    }

    filterTasks(tasks) {
        const results = [];
        for (const task of tasks) {
            const matches = task.title.toLowerCase().includes(this.searchQuery) ||
                           (task.notes && task.notes.toLowerCase().includes(this.searchQuery));
            const childMatches = task.children ? this.filterTasks(task.children) : [];

            if (matches || childMatches.length > 0) {
                results.push({
                    ...task,
                    children: childMatches.length > 0 ? childMatches : task.children
                });
                // Auto-expand matching tasks
                if (childMatches.length > 0) {
                    this.expandedTasks.add(task.id);
                }
            }
        }
        return results;
    }

    renderTaskList(tasks, isRoot = true) {
        const items = tasks.map(task => this.renderTaskItem(task)).join('');
        return isRoot ? `<div class="tree-root">${items}</div>` : items;
    }

    renderTaskItem(task) {
        const hasChildren = task.children && task.children.length > 0;
        const isExpanded = this.expandedTasks.has(task.id);
        const childStats = hasChildren ? this.calculateStats(task.children) : null;
        const priority = task.priority || 'medium';

        return `
            <div class="tree-item" data-id="${task.id}">
                <div class="tree-item-content ${task.completed ? 'completed' : ''}">
                    <div class="priority-indicator ${priority}"></div>
                    <button class="tree-toggle ${hasChildren ? '' : 'hidden'} ${isExpanded ? 'expanded' : ''}"
                            data-action="toggle" data-id="${task.id}">
                        <svg width="10" height="10" viewBox="0 0 10 10">
                            <path d="M3 1 L7 5 L3 9" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
                        </svg>
                    </button>
                    <input type="checkbox" class="tree-check"
                           ${task.completed ? 'checked' : ''}
                           data-action="complete" data-id="${task.id}">
                    <span class="tree-title" data-action="edit" data-id="${task.id}">${this.escapeHtml(task.title)}</span>
                    <div class="tree-meta">
                        ${childStats ? `<span class="child-count">${childStats.completed}/${childStats.total}</span>` : ''}
                        ${task.category && task.category !== 'general' ? `<span class="tag tag-${task.category}">${task.category}</span>` : ''}
                    </div>
                    <div class="tree-actions">
                        <button class="action-btn" data-action="add-child" data-id="${task.id}" title="Add subtask">+</button>
                        <button class="action-btn delete" data-action="delete" data-id="${task.id}" title="Delete">&times;</button>
                    </div>
                </div>
                ${task.notes ? `<div class="tree-notes">${this.escapeHtml(task.notes)}</div>` : ''}
                ${hasChildren && isExpanded ? `<div class="tree-children">${this.renderTaskList(task.children, false)}</div>` : ''}
            </div>
        `;
    }

    attachTaskEventListeners() {
        document.querySelectorAll('[data-action]').forEach(el => {
            el.addEventListener('click', (e) => {
                e.stopPropagation();
                const action = el.dataset.action;
                const id = el.dataset.id;

                switch (action) {
                    case 'toggle': this.toggleExpand(id); break;
                    case 'complete': this.toggleComplete(id); break;
                    case 'edit': this.openModal(id, null); break;
                    case 'add-child': this.openModal(null, id); break;
                    case 'delete': this.deleteTask(id); break;
                }
            });
        });
    }

    calculateStats(tasks) {
        let total = 0, completed = 0;
        const count = (items) => {
            for (const task of items) {
                total++;
                if (task.completed) completed++;
                if (task.children) count(task.children);
            }
        };
        count(tasks);
        return { total, completed };
    }

    // Task operations
    toggleExpand(id) {
        if (this.expandedTasks.has(id)) {
            this.expandedTasks.delete(id);
        } else {
            this.expandedTasks.add(id);
        }
        this.saveData();
        this.renderTasks();
    }

    toggleComplete(id) {
        const task = this.findTask(id, this.tasks);
        if (task) {
            task.completed = !task.completed;
            // Optionally complete/uncomplete all children
            if (task.children) {
                this.setAllCompleted(task.children, task.completed);
            }
            this.saveData();
            this.renderTasks();
        }
    }

    setAllCompleted(tasks, completed) {
        tasks.forEach(task => {
            task.completed = completed;
            if (task.children) this.setAllCompleted(task.children, completed);
        });
    }

    findTask(id, tasks) {
        for (const task of tasks) {
            if (task.id === id) return task;
            if (task.children) {
                const found = this.findTask(id, task.children);
                if (found) return found;
            }
        }
        return null;
    }

    findParentTask(id, tasks, parent = null) {
        for (const task of tasks) {
            if (task.id === id) return parent;
            if (task.children) {
                const found = this.findParentTask(id, task.children, task);
                if (found !== undefined) return found;
            }
        }
        return undefined;
    }

    deleteTask(id) {
        if (!confirm('Delete this task and all subtasks?')) return;

        const deleteFromList = (tasks) => {
            const index = tasks.findIndex(t => t.id === id);
            if (index !== -1) {
                tasks.splice(index, 1);
                return true;
            }
            for (const task of tasks) {
                if (task.children && deleteFromList(task.children)) return true;
            }
            return false;
        };

        deleteFromList(this.tasks);
        this.expandedTasks.delete(id);
        this.saveData();
        this.renderTasks();
    }

    // Modal
    openModal(editId, parentId) {
        this.editingTaskId = editId;
        this.editingParentId = parentId;

        const modal = document.getElementById('task-modal');
        const form = document.getElementById('task-form');
        form.reset();

        if (editId) {
            const task = this.findTask(editId, this.tasks);
            if (task) {
                document.getElementById('modal-title').textContent = 'Edit Task';
                document.getElementById('task-title').value = task.title;
                document.getElementById('task-notes').value = task.notes || '';
                document.getElementById('task-priority').value = task.priority || 'medium';
                document.getElementById('task-category').value = task.category || 'general';
            }
        } else {
            document.getElementById('modal-title').textContent = parentId ? 'Add Subtask' : 'New Task';
        }

        modal.classList.add('active');
        document.getElementById('task-title').focus();
    }

    closeModal() {
        document.getElementById('task-modal').classList.remove('active');
        this.editingTaskId = null;
        this.editingParentId = null;
    }

    saveTask() {
        const title = document.getElementById('task-title').value.trim();
        if (!title) return;

        const taskData = {
            title,
            notes: document.getElementById('task-notes').value.trim(),
            priority: document.getElementById('task-priority').value,
            category: document.getElementById('task-category').value,
            completed: false,
            children: []
        };

        if (this.editingTaskId) {
            // Edit existing
            const task = this.findTask(this.editingTaskId, this.tasks);
            if (task) {
                Object.assign(task, taskData, {
                    id: task.id,
                    completed: task.completed,
                    children: task.children
                });
            }
        } else if (this.editingParentId) {
            // Add as child
            const parent = this.findTask(this.editingParentId, this.tasks);
            if (parent) {
                if (!parent.children) parent.children = [];
                taskData.id = this.generateId();
                parent.children.push(taskData);
                this.expandedTasks.add(this.editingParentId);
            }
        } else {
            // Add as root
            taskData.id = this.generateId();
            this.tasks.push(taskData);
        }

        this.saveData();
        this.closeModal();
        this.renderTasks();
    }

    // Wiki
    renderWiki(search = '') {
        const nav = document.getElementById('wiki-nav');
        const content = document.getElementById('wiki-content');

        const filtered = search
            ? this.wikiPages.filter(p =>
                p.title.toLowerCase().includes(search) ||
                p.content.toLowerCase().includes(search))
            : this.wikiPages;

        if (filtered.length === 0) {
            nav.innerHTML = '<p class="empty">No docs found</p>';
            content.innerHTML = '<p class="empty">No documentation available</p>';
            return;
        }

        nav.innerHTML = filtered.map((page, i) => `
            <button class="wiki-link ${i === 0 ? 'active' : ''}" data-page="${page.id}">${this.escapeHtml(page.title)}</button>
        `).join('');

        // Show first page
        this.showWikiPage(filtered[0].id);

        // Attach click handlers
        nav.querySelectorAll('.wiki-link').forEach(link => {
            link.addEventListener('click', () => {
                nav.querySelectorAll('.wiki-link').forEach(l => l.classList.remove('active'));
                link.classList.add('active');
                this.showWikiPage(link.dataset.page);
            });
        });
    }

    showWikiPage(id) {
        const page = this.wikiPages.find(p => p.id === id);
        if (page) {
            document.getElementById('wiki-content').innerHTML = page.content;
        }
    }

    // Export/Import/Reset
    exportData() {
        const data = { tasks: this.tasks, wikiPages: this.wikiPages, version: '2.0' };
        const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `zues-tracker-${new Date().toISOString().split('T')[0]}.json`;
        a.click();
        URL.revokeObjectURL(url);
    }

    importData() {
        const input = document.getElementById('file-input');
        input.onchange = (e) => {
            const file = e.target.files[0];
            if (!file) return;
            const reader = new FileReader();
            reader.onload = (event) => {
                try {
                    const data = JSON.parse(event.target.result);
                    if (confirm('Replace all data with imported file?')) {
                        this.tasks = data.tasks || [];
                        this.wikiPages = data.wikiPages || [];
                        this.expandedTasks.clear();
                        this.tasks.forEach(t => this.expandedTasks.add(t.id));
                        this.saveData();
                        this.render();
                    }
                } catch (err) {
                    alert('Invalid file format');
                }
            };
            reader.readAsText(file);
            input.value = '';
        };
        input.click();
    }

    resetData() {
        if (confirm('Reset all data to defaults? This cannot be undone.')) {
            localStorage.removeItem('zuesTrackerV2');
            this.tasks = [];
            this.wikiPages = [];
            this.expandedTasks.clear();
            this.initializeDefaultData();
            this.render();
        }
    }

    reloadDefaults() {
        if (!window.defaultData) {
            alert('No default data available');
            return;
        }

        if (confirm('Reload tasks from defaults? This will REPLACE all tasks with the latest defaults from data.js. Your current tasks will be lost. Wiki pages will also be updated.')) {
            // Replace tasks with defaults
            this.tasks = JSON.parse(JSON.stringify(window.defaultData.tasks || []));
            // Update wiki pages
            this.wikiPages = window.defaultData.wikiPages || [];
            // Expand root tasks
            this.expandedTasks.clear();
            this.tasks.forEach(t => this.expandedTasks.add(t.id));
            this.saveData();
            this.render();
            alert('Tasks and wiki reloaded from defaults!');
        }
    }

    // Utilities
    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }
}

// Initialize
const tracker = new ProjectTracker();
