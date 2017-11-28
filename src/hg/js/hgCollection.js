// hgCollection.js - Interactive features for GTEX Body Map version of GTEx Gene track UI page

// Copyright (C) 2017 The Regents of the University of California

var hgCollection = (function() {
    var names = []; // a list of names that have been used
    var selectedNode = "collectionList"; // keep track of id of selected row
    var selectedTree = "collectionList"; // keep track of id of selected row
    var $tracks;  // the #tracks object
    var trees = [];
    var isDirty = false;
    var goTracks = false;
    var doAjaxAsync = true;

    function currentTrackItems(node) {
        // populate the menu for the currentCollection tree
        var items = {
            addItem: { // The "add" menu item
                label: "Add",
                action: function () {
                    var nodeIds = $("#tracks").jstree( "get_selected");
                    isDirty = true;
                    var nodes = [];
                    var node;
                    for(ii=0; ii < nodeIds.length; ii++) {
                        node = $("#tracks").jstree('get_node', nodeIds[ii]);
                        if (node.children.length === 0)
                            nodes.push(node);
                    }
                    var parentId = $(selectedNode).attr('id');
                    $(selectedTree).jstree("copy_node", nodes, parentId,'last');
                }
            }
        };

        if ($(node).attr('children').length > 0)
            delete items.addItem;

        return items;
    }

    function currentCollectionItems(node) {
        // populate the menu for the currentCollection tree
        var items = {
            deleteItem: { // The "delete" menu item
                label: "Delete",
                action: function () {
                    var nodes = $(selectedTree).jstree( "get_selected");
                    isDirty = true;
                    $(selectedTree).jstree( "delete_node", nodes);
                }
            }
        };

        // can't delete root
        if ($(node).attr('parent') === '#')
            delete items.deleteItem;

        return items;
        }

    function selectElements (selectableContainer, elementsToSelect) {
        // add unselecting class to all elements in the styleboard canvas except the ones to select
        $(".ui-selected", selectableContainer).not(elementsToSelect).removeClass("ui-selected").addClass("ui-unselecting");

        // add ui-selecting class to the elements to select
        $(elementsToSelect).not(".ui-selected").addClass("ui-selecting");

        // trigger the mouse stop event (this will select all .ui-selecting elements, and deselect all .ui-unselecting elements)
        selectableContainer.data("ui-selectable").refresh();
        selectableContainer.data("ui-selectable")._mouseStop(null);
    }

    function selectNode(tree, node) {
        // called when a node in the currentCollection tree is selected
        var color = node.li_attr.color;
        var name =  node.li_attr.shortlabel;
        var description = node.li_attr.longlabel;
        var visibility = node.li_attr.visibility;
        var type = node.li_attr.viewtype;
        var viewFunc = node.li_attr.viewfunc;
        selectedNode = node;
        selectedTree = tree;

        if (type == 'view') 
            $("#viewFuncDiv").show();
        else
            $("#viewFuncDiv").hide();

        $("#CustomTrackOptions").show();
        $("#viewFunc").val(viewFunc);
        $("#customName").val(name);
        $("#customDescription").val(description);
        $("#customVis").val(visibility);
        $("#customColorInput").val(color);
        $("#customColorPicker").spectrum("set", color);
        $(selectedTree).jstree("toggle_node", selectedNode);
   }

    function selectTreeNode(evt, data)             {
        selectNode(evt.target, data.node);
    }

    function checkCallback( operation, node, node_parent, node_position, more) {
        // called during a drag and drop action to see if the target is droppable
        if ((operation === "copy_node") ||  (operation === "move_node")) {
            if (node_parent.li_attr.class !== "folder")
                return false;
        }

        return true;
    }

    function dialogCalcTrack() {
        $( "#newCalcTrackDialog" ).dialog("open");
    } 

    function newCalcTrack() {
        // create a new view under a collection
        $( "#newCalcTrackDialog" ).dialog("close");
        var ourCalcName = getUniqueName("calc");
        var newName = "Calc Track";
        var newDescription = "Description of Calculated Track";
        var parent = $(selectedTree).find("li").first();
        var children = $(selectedTree).jstree("get_children_dom",$(selectedTree).jstree("get_node", $(parent).attr('id')));

        var newId = $(selectedTree).jstree("create_node", parent, newName + " (" + newDescription + ")");
        var newNode = $(selectedTree).jstree("get_node", newId);
        isDirty = true;
        newNode.li_attr.class = "folder";
        newNode.li_attr.name = ourCalcName;
        newNode.li_attr.shortlabel = newName;
        newNode.li_attr.longlabel = newDescription;
        newNode.li_attr.visibility = "full";
        newNode.li_attr.color = "#0";
        newNode.li_attr.missingMethod = $("input:radio[name ='missingData']:checked").val();
        newNode.li_attr.viewfunc = "add all";
        newNode.li_attr.viewtype = "view";
        $(selectedTree).jstree("set_icon", newNode, '../images/folderC.png');

        var nodes = [];
        for(ii=0; ii < children.length; ii++) {
            if (!$(children[ii]).hasClass('folder'))
                nodes.push(children[ii]);
        }

        switch($("input:radio[name ='defaultContents']:checked").val()) {
            case "move":
                // move_node causes havoc
                $(selectedTree).jstree('copy_node', nodes, newNode, 'last');
                $(selectedTree).jstree('delete_node', nodes);
                break;
            case "copy":
                $(selectedTree).jstree('copy_node', nodes, newNode);
                break;
            case "empty":
                break;
        }
    }

    function newCollection() {
        isDirty = true;
        // called when the "New Collection" button is pressed
        var ourCollectionName = getUniqueName("coll");
        var ourTreeName = getUniqueName("tree");
        var newName = "A New Collection";
        var newDescription = "Description of New Collection";
        var attributes = "shortLabel='" +  newName + "' ";
        attributes += "longLabel='" +  newDescription + "' ";
        attributes += "color='" + "#0" + "' ";
        attributes += "viewType='" + "track" + "' ";
        attributes += "visibility='" + "full" + "' ";
        attributes += "name='" +  ourCollectionName + "' ";
        attributes += "class='" +  "folder" + "' ";

        $('#collectionList').append("<li " + attributes +  "id='"+ourCollectionName+"'>A New Collection</li>");
        $('#currentCollection').append("<div id='"+ourTreeName+"'><ul><li data-jstree='{\"icon\":\"../images/folderC.png\"}' " + attributes+ ">A New Collection</li><ul></div>");
        var newTree = $('#currentCollection div:last');
        trees[ourCollectionName] = newTree;
        newTree.jstree({
               "core" : {
                     "check_callback" : checkCallback
                         },
               'plugins' : ['dnd', 'conditionalselect', 'contextmenu'],
               'check_callback' : checkCallback,
               'contextmenu': { "items" : currentCollectionItems},
               'dnd': {check_while_dragging: true}
        });
        newTree.on("select_node.jstree", selectTreeNode);
        newTree.on("copy_node.jstree", function (evt, data)  {
            $(evt.target).jstree("open_node", data.parent);
            $(evt.target).jstree("set_icon", data.node, 'fa fa-minus-square');
        });
        newTree.on('click', '.jstree-themeicon ', minusHit);
        var lastElement = $("#collectionList li").last();
        //lastElement.addClass("folder");
        selectElements($("#collectionList"), lastElement) ;
        rebuildLabel();
    }

    function hideAllTrees() {
        // hide all the trees in the Collected Tracks window
        for(var key in trees)
            trees[key].hide();
    }

    function selectCollection(event, ui ) {
        // called with a collection is selected
        var id = ui.selected.id;
        $('#collectedTracksTitle').text(ui.selected.innerText);
        hideAllTrees();
        trees[id].show();
        var node = trees[id].find("li").first();
        trees[id].jstree('select_node', node);
        selectNode(trees[id], trees[id].jstree("get_node",node));
    }

    function addCollection(trees, list) {
        // called when outputting JSON of all the collectionList
        var collectTree = trees[list.id];
        var v = collectTree.jstree(true).get_json('#', {flat:true, no_data:true, no_state:true, no_a_attr:true});
        var mytext = JSON.stringify(v);
        return mytext;
    }

    function saveCollections(trees) {
       // called when the "Save" button is pressed
       var json = "[";
       $('#collectionList li').each(function() {
            json += addCollection(trees, this ) + ',';
        });
        json = json.slice(0, -1);
        json += ']';
        var requestData = 'jsonp=' + json;
        $.ajax({
            data:  requestData ,
            async: doAjaxAsync,
            dataType: "JSON",
            type: "PUT",
            url: "hgCollection?cmd=saveCollection",
            trueSuccess: updatePage,
            success: catchErrorOrDispatch,
            error: errorHandler,
        });
    }

    function rebuildLabel() {
        // rebuild the label for tree item
        var newText = selectedNode.li_attr.shortlabel + "   (" + selectedNode.li_attr.longlabel + ")";
        $(selectedTree).jstree('rename_node', selectedNode, newText);
    }

    function descriptionChange() {
        // change the description (longLabel) for a track
        selectedNode.li_attr.longlabel = $("#customDescription").val();
        isDirty = true;
        rebuildLabel();
    }

    function viewFuncChange() {
        // change the view function for a track
        isDirty = true;
        selectedNode.li_attr.viewfunc = $("#viewFunc").val();
        }

    function nameChange() {
        // change the name (shortLabel)  of a track
        isDirty = true;
        selectedNode.li_attr.shortlabel = $("#customName").val();
        rebuildLabel();
        if (selectedNode.parent === '#') {
            $("#collectionList .ui-selected").text($("#customName").val());
            $('#collectedTracksTitle').text($("#customName").val());
        }
    }

    function colorChange() {
        // change the color for a track
        isDirty = true;
        var color = $("#customColorPicker").spectrum("get"); $('#customColorInput').val(color);
        selectedNode.li_attr.color = $("#customColorInput").val();
    }

    function visChange() {
        // change the visibility of a track
        isDirty = true;
        selectedNode.li_attr.visibility = $("#customVis").val();
    }

    function isDraggable(nodes) {
        // only children can be dragged
        var ii;
        for (ii=0; ii < nodes.length; ii++)
            if (nodes[ii].children.length !== 0)
                return false;
        return true;
    }

    function collectionListRightClick (event) {
        // popup the right menu in the collection list
        $(".collectionList-menu").finish().toggle(100).css({
            top: event.pageY + "px",
            left: event.pageX + "px"
        });
        return false;
    }

    function recordNames(tree) {
        // keep an accounting of track names that have been used
        var v = $(tree).jstree(true).get_json('#', {'flat': true});
        for (i = 0; i < v.length; i++) {
            var z = v[i];
            names[z.li_attr.name] = 1;
        }
    }

    function plusHit(event, data) {
        // called with the plus icon is hit
        var treeObject = $(event.currentTarget).parent().parent();
        var id = treeObject.attr('id');
        var node = treeObject.jstree("get_node", id);
        if (node.children.length === 0) {
            var parentId = $(selectedNode).attr('id');
            isDirty = true;
            $(selectedTree).jstree("copy_node", node, parentId,'last');
        }
    }

    function minusHit (event, data) {
        // called with the minus icon is hit
        var treeObject = $(event.currentTarget).parent().parent();
        var id = treeObject.attr('id');
        var node = treeObject.jstree("get_node", id);
        if (node.children.length === 0) {
            isDirty = true;
            $(selectedTree).jstree( "delete_node", node);
        }
    }

    function init() {
        $body = $("body");

        // block user input when ajax is running
        $(document).on({
            ajaxStart: function() { $body.addClass("loading");    },
            ajaxStop: function() { $body.removeClass("loading"); }    
        });

        $('.gbButtonGoContainer').click(submitForm);
       
        window.addEventListener("beforeunload", function (e) {
            if (isDirty) {
                doAjaxAsync = false;
                saveCollections(trees);
            }

            return undefined;
        });

        // called at initialization time
        $("#viewFunc").change(viewFuncChange);
        $("#customName").change(nameChange);
        $("#customDescription").change(descriptionChange);
        $("#customVis").change(visChange);
        //$("#customColorInput").change(colorChange);
        $("#saveCollections").click ( function() { saveCollections(trees); } );
        $("#discardChanges").click ( function () { isDirty = false; window.location.reload(); });

        $("#newCollection").click ( newCollection );
        $( "#newCalcTrackDialog" ).dialog({ modal: true, 
            width: "50%", 
            autoOpen: false,
            });
        $("#newCalcTrackButton").click ( dialogCalcTrack );
        $("#newCalcTrack").click ( newCalcTrack );
        $('#collectionList').selectable({selected : selectCollection});
        
        $( "#collectionList" ).contextmenu(collectionListRightClick);

        $(document).bind("mousedown", function (e) {
                // If the clicked element is not the menu
                if ($(e.target).parents(".collectionList-menu").length === 0) {
                    // Hide it
                $(".collectionList-menu").hide(100);
                }
        });

        $(".collectionList-menu li").click(function(){
            // This is the triggered action name
            switch($(this).attr("data-action")) {
                case "delete": 
                    $("#collectionList .ui-selected").remove();
                    var firstElement = $("#collectionList li").first();
                    if (firstElement.length !== 0)
                        selectElements($("#collectionList"), firstElement) ;
                    else 
                        $(selectedTree).remove();
            }

            // Hide it AFTER the action was triggered
            $(".collectionList-menu").hide(100);
        });

        var trackOpt = {
            hideAfterPaletteSelect : true,
            color : $('#customColorInput').val(),
            showPalette: true,
            showInput: true,
            preferredFormat: "hex",
            change: colorChange,
        };

        $("#customColorPicker").spectrum(trackOpt);
        $.jstree.defaults.core.check_callback = checkCallback;
        $.jstree.defaults.core.themes.dots = true;
        $.jstree.defaults.contextmenu.show_at_node = false;
        $("#currentCollection div").each(function(index) {
            var newTree = this;

            $(newTree).jstree({
               'plugins' : ['dnd', 'conditionalselect', 'contextmenu'],
               'contextmenu': { "items" : currentCollectionItems},
               'dnd': {
                "check_callback" : checkCallback,
                }
            });
            recordNames(newTree);
            trees[this.id] = $(newTree);
            $(newTree).on("select_node.jstree", selectTreeNode);
            $(newTree).on("copy_node.jstree", function (evt, data)  {
                $(evt.target).jstree("open_node", data.parent);
                $(evt.target).jstree("set_icon", data.node, 'fa fa-minus-square');
            });
            $(newTree).on('click', '.jstree-themeicon ', minusHit);
        });

        treeDiv=$('#tracks');
        treeDiv.jstree({
               'plugins' : ['dnd', 'conditionalselect', 'contextmenu'],
               'contextmenu': { "items" : currentTrackItems},
               'dnd': {
                "check_callback" : checkCallback,
               'always_copy' : true,
                is_draggable: isDraggable,
               },
               'core' :  {
                "check_callback" : checkCallback
            }
        });
        treeDiv.on("select_node.jstree", function (evt, data)  {
            $(evt.target).jstree("toggle_node", data.node);
        });
        treeDiv.on('click', '.jstree-themeicon ', plusHit);

        var firstElement = $("#collectionList li").first();
        selectElements($("#collectionList"), firstElement) ;
    }

   function submitForm() {
    // Submit the form (from GO button -- as in hgGateway.js)
    // Show a spinner -- sometimes it takes a while for hgTracks to start displaying.
        $('.gbIconGo').removeClass('fa-play').addClass('fa-spinner fa-spin');
        goTracks = true;
        saveCollections(trees);
    }

    function updatePage(responseJson) {
        // called after AJAX call
        isDirty = false;
        if (!responseJson) {
            return;
        }

        if (goTracks) {
            // we go straight to hgTracks after save
            $form = $('form');
            $form.submit();
        }
    }

    function getUniqueName(root) {
        // make sure name is unique in track hub
        if (!names[root]) {
            names[root] = true;
            return root;
        } else {
            var counter = 1;

            for(; ; counter++) {
                var name  = root + counter;
                if (!names[name]) {
                    names[name] = true;
                    return name;
                }
            }
        }
    }

    return {
        init: init
    };
}());
