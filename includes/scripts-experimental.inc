<?php

/**
 * @file
 * A file to contain functions for the magic module to abuse.
 */

/**
 * Returns a themed presentation of all JavaScript code for the current page.
 *
 * References to JavaScript files are placed in a certain order: first, all
 * 'core' files, then all 'module' and finally all 'theme' JavaScript files
 * are added to the page. Then, all settings are output, followed by 'inline'
 * JavaScript code. If running update.php, all preprocessing is disabled.
 *
 * Note that hook_js_alter(&$javascript) is called during this function call
 * to allow alterations of the JavaScript during its presentation. Calls to
 * magic_add_js() from hook_js_alter() will not be added to the output
 * presentation. The correct way to add JavaScript during hook_js_alter()
 * is to add another element to the $javascript array, deriving from
 * drupal_js_defaults(). See locale_js_alter() for an example of this.
 *
 * @param $scope
 *   (optional) The scope for which the JavaScript rules should be returned.
 *   Defaults to 'header'.
 * @param $javascript
 *   (optional) An array with all JavaScript code. Defaults to the default
 *   JavaScript array for the given scope.
 * @param $skip_alter
 *   (optional) If set to TRUE, this function skips calling drupal_alter() on
 *   $javascript, useful when the calling function passes a $javascript array
 *   that has already been altered.
 *
 * @return
 *   All JavaScript code segments and includes for the scope as HTML tags.
 *
 * @see magic_add_js()
 * @see locale_js_alter()
 * @see drupal_js_defaults()
 */
function magic_experimental_js($scope = 'header', $javascript = NULL, $skip_alter = FALSE) {
  if (!isset($javascript)) {
    $javascript = magic_add_js();
  }
  if (empty($javascript)) {
    return '';
  }

  // Allow modules to alter the JavaScript.
  if (!$skip_alter) {
    drupal_alter('js', $javascript);
  }

  // Check to see if Force Header is available and set to true.
  // If it is neither enabled nor set to true, change scope to footer,
  // otherwise, keep it in the header.
  // Done again here because scope may change on alter.
  foreach ($javascript as $js_key => $js_value) {
    if (!empty($js_value['force header'])) {
      $javascript[$js_key]['scope'] = 'header';
    }
    else {
      $javascript[$js_key]['scope'] = 'footer';
    }
  }

  // Check to see if Force Header is available and set to true.
  // If it is neither enabled nor set to true, change scope to footer, otherwise, keep it in the header.
  // Done here because scope may change on alter. Screw Inversion of control.

  // Get Magic Library Head variable for current theme.
  $library_head = theme_get_setting('magic_library_head');
  $footer_js = theme_get_setting('magic_footer_js');

  if ($footer_js) {
    foreach ($javascript as $js_key => $js_value) {
      if (!empty($js_value['force header']) && $js_value['force header']) {
        $javascript[$js_key]['scope'] = 'header';
      }
      else {
        $javascript[$js_key]['scope'] = 'footer';
      }

      if ($js_value['group'] == JS_LIBRARY && $library_head) {
        $javascript[$js_key]['scope'] = 'header';
      }

      if ($js_key == 'settings') {
        $javascript[$js_key]['scope'] = 'header';
      }
    }
  }

  // Filter out elements of the given scope.
  $items = array();
  foreach ($javascript as $key => $item) {
    if ($item['scope'] == $scope) {
      $items[$key] = $item;
    }
  }

  // Sort the JavaScript so that it appears in the correct order.
  uasort($items, 'magic_sort_css_js');

  // In Drupal 8, there's a JS_SETTING group for making setting variables
  // appear last after libraries have loaded. In Drupal 7, this is forced
  // without that group. We do not use the $key => $item type of iteration,
  // because PHP uses an internal array pointer for that, and we're modifying
  // the array order inside the loop.
  foreach (array_keys($items) as $key) {
    if (!empty($items[$key]['type']) && $items[$key]['type'] == 'setting') {
      $item = $items[$key];
      unset($items[$key]);
      $items[$key] = $item;
    }
  }

  // Provide the page with information about the individual JavaScript files
  // used, information not otherwise available when aggregation is enabled.
  $setting['ajaxPageState']['js'] = array_fill_keys(array_keys($items), 1);
  unset($setting['ajaxPageState']['js']['settings']);
  drupal_add_js($setting, 'setting');

  // If we're outputting the header scope, then this might be the final time
  // that drupal_get_js() is running, so add the setting to this output as well
  // as to the drupal_add_js() cache. If $items['settings'] doesn't exist, it's
  // because drupal_get_js() was intentionally passed a $javascript argument
  // stripped of settings, potentially in order to override how settings get
  // output, so in this case, do not add the setting to this output.
  if ($scope == 'header' && isset($items['settings'])) {
    $items['settings']['data'][] = $setting;
  }

  // Render the HTML needed to load the JavaScript.
  $elements = array(
    '#type' => 'scripts',
    '#items' => $items,
  );

  return drupal_render($elements);
}

/**
 * Default callback to group JavaScript items.
 *
 * This function arranges the JavaScript items that are in the #items property
 * of the scripts element into groups. When aggregation is enabled, files within
 * a group are aggregated into a single file, significantly improving page
 * loading performance by minimizing network traffic overhead.
 *
 * This function puts multiple items into the same group if they are groupable
 * and if they are for the same browsers. Items of the 'file' type are groupable
 * if their 'preprocess' flag is TRUE. Items of the 'inline', 'settings', or
 * 'external' type are not groupable.
 *
 * This function also ensures that the process of grouping items does not change
 * their relative order. This requirement may result in multiple groups for the
 * same type and browsers, if needed to accommodate other items in
 * between.
 *
 * @param $javascript
 *   An array of JavaScript items, as returned by drupal_add_js(), but after
 *   alteration performed by drupal_get_js().
 *
 * @return
 *   An array of JavaScript groups. Each group contains the same keys (e.g.,
 *   'data', etc.) as a JavaScript item from the $javascript parameter, with the
 *   value of each key applying to the group as a whole. Each group also
 *   contains an 'items' key, which is the subset of items from $javascript that
 *   are in the group.
 *
 * @see drupal_pre_render_scripts()
 */
function magic_group_js($javascript) {
  $groups = array();
  // If a group can contain multiple items, we track the information that must
  // be the same for each item in the group, so that when we iterate the next
  // item, we can determine if it can be put into the current group, or if a
  // new group needs to be made for it.
  $current_group_keys = NULL;
  $index = -1;

  $ordering_array = array();

  foreach ($javascript as &$item) {
    $item['browsers'] = isset($item['browsers']) ? $item['browsers'] : array();
    $item['browsers'] += array(
      'IE'
    );

    // As these are not native in Drupal, we add in the default values.
    $item += array(
      'defer' => FALSE,
      'async' => FALSE,
      'type' => '',
    );

    // The browsers for which the JavaScript item needs to be loaded is part of
    // the information that determines when a new group is needed, but the order
    // of keys in the array doesn't matter, and we don't want a new group if all
    // that's different is that order.
    ksort($item['browsers']);

    // We are first going to ensure that all the javascript is in the right
    // order.
    $group = $item['group'];

    $weight = $item['weight'];
    // We are chaging the weight of the files, so that ones flagged with
    // every_page are put first.
    $weight += $item['every_page'] ? -10000 : 10000;
    $grouped_items[$group][] = $item;
    $ordering_array[$group][] = $weight;
  }

  // This will sort all the groups to ensure we are in the right order.
  ksort($grouped_items);
  ksort($ordering_array);

  foreach ($grouped_items as $group => $items) {
    // Now we go into each group, and sort by the individual weight. This will
    // also take into effect the every_page tag.
    array_multisort($ordering_array[$group], SORT_NUMERIC, $items);

    foreach ($items as $item) {
      switch ($item['type']) {
        case 'file':
          // Group file items if their 'preprocess' flag is TRUE.
          // Help ensure maximum reuse of aggregate files by only grouping
          // together items that share the same 'group' value and 'every_page'
          // flag. See drupal_add_js() for details about that.
          $group_keys = !empty($item['preprocess']) ? array($item['every_page'], $item['browsers'], $item['defer']) : FALSE;
        break;

        case 'external':
        case 'inline':
        case 'setting':
        default:
          // Do not group external, settings, and inline items.
          $group_keys = FALSE;
        break;
      }

      // If the group keys don't match the most recent group we're working with,
      // then a new group must be made. Also, each inline, external, or setting
      // script will also be in their own group.
      if ((!empty($group_keys) && $group_keys !== $current_group_keys) || $group_keys == FALSE) {
        $index++;
        // We set the base settings to the first object, for reference later on.
        $groups[$index] = $item;
        $current_group_keys = $group_keys;
      }

      $groups[$index]['items'][] = $item;
    }
  }

  return $groups;
}

/**
 * #pre_render callback to add the elements needed for JavaScript tags to be
 * rendered.
 *
 * This function evaluates the aggregation enabled/disabled condition on a group
 * by group basis by testing whether an aggregate file has been made for the
 * group rather than by testing the site-wide aggregation setting. This allows
 * this function to work correctly even if modules have implemented custom
 * logic for grouping and aggregating files.
 *
 * @param $element
 *   A render array containing:
 *   - #items: The JavaScript items as returned by drupal_add_js() and
 *     altered by drupal_get_js().
 *   - #group_callback: A function to call to group #items. Following
 *     this function, #aggregate_callback is called to aggregate items within
 *     the same group into a single file.
 *   - #aggregate_callback: A function to call to aggregate the items within
 *     the groups arranged by the #group_callback function.
 *
 * @return
 *   A render array that will render to a string of JavaScript tags.
 *
 * @see drupal_get_js()
 */
function magic_pre_render_scripts($elements) {

  // Group and aggregate the items.
  if (isset($elements['#group_callback'])) {
    $elements['#groups'] = $elements['#group_callback']($elements['#items']);
  }
  if (isset($elements['#aggregate_callback'])) {
    $elements['#aggregate_callback']($elements['#groups']);
  }

  // A dummy query-string is added to filenames, to gain control over
  // browser-caching. The string changes on every update or full cache
  // flush, forcing browsers to load a new copy of the files, as the
  // URL changed. Files that should not be cached (see drupal_add_js())
  // get REQUEST_TIME as query-string instead, to enforce reload on every
  // page request.
  $default_query_string = variable_get('css_js_query_string', '0');

  // For inline JavaScript to validate as XHTML, all JavaScript containing
  // XHTML needs to be wrapped in CDATA. To make that backwards compatible
  // with HTML 4, we need to comment out the CDATA-tag.
  $embed_prefix = "\n<!--//--><![CDATA[//><!--\n";
  $embed_suffix = "\n//--><!]]>\n";

  // Since JavaScript may look for arguments in the URL and act on them, some
  // third-party code might require the use of a different query string.
  $js_version_string = variable_get('drupal_js_version_query_string', 'v=');

  // Defaults for each SCRIPT element.
  $element_defaults = array(
    '#type' => 'html_tag',
    '#tag' => 'script',
    '#value' => '',
    '#attributes' => array(
      'type' => 'text/javascript',
    ),
  );

  // Loop through each group.
  foreach ($elements['#groups'] as $group) {
    // If a group of files has been aggregated into a single file,
    // $group['data'] contains the URI of the aggregate file. Add a single
    // script element for this file.
    if (!empty($group['type']) && $group['type'] == 'file' && isset($group['data'])) {
      $element = $element_defaults;
      $element['#attributes']['src'] = file_create_url($group['data']);
      $element['#browsers'] = $group['browsers'];
      $elements[] = $element;
    }
    // For non-file types, and non-aggregated files, add a script element per
    // item.
    else {
      foreach ($group['items'] as $item) {
        // Element properties that do not depend on item type.
        $element = $element_defaults;
        if (!empty($item['defer'])) {
          $element['#attributes']['defer'] = 'defer';
        }
        if (!empty($item['async'])) {
          $element['#attributes']['async'] = 'async';
        }
        $element['#browsers'] = $item['browsers'];

        if (!empty($item['type'])) {
          // Element properties that depend on item type.
          switch ($item['type']) {
            case 'setting':
              $element['#value_prefix'] = $embed_prefix;
              $element['#value'] = 'jQuery.extend(Drupal.settings, ' . drupal_json_encode(drupal_array_merge_deep_array($item['data'])) . ");";
              $element['#value_suffix'] = $embed_suffix;
              break;

            case 'inline':
              $element['#value_prefix'] = $embed_prefix;
              $element['#value'] = $item['data'];
              $element['#value_suffix'] = $embed_suffix;
              break;

            case 'file':
              $query_string = empty($item['version']) ? $default_query_string : $js_version_string . $item['version'];
              $query_string_separator = (strpos($item['data'], '?') !== FALSE) ? '&' : '?';
              $element['#attributes']['src'] = file_create_url($item['data']) . $query_string_separator . ($item['cache'] ? $query_string : REQUEST_TIME);
              break;

            case 'external':
              $element['#attributes']['src'] = $item['data'];
              break;
          }
        }

        $elements[] = $element;
      }
    }
  }
  return $elements;
}

/**
 * Default callback to aggregate JavaScript files.
 *
 * Having the browser load fewer JavaScript files results in much faster page
 * loads than when it loads many small files. This function aggregates files
 * within the same group into a single file unless the site-wide setting to do
 * so is disabled (commonly the case during site development). To optimize
 * download, it also compresses the aggregate files by removing comments,
 * whitespace, and other unnecessary content.
 *
 * @param $js_groups
 *   An array of JavaScript groups as returned by drupal_group_js(). For each
 *   group that is aggregated, this function sets the value of the group's
 *   'data' key to the URI of the aggregate file.
 *
 * @see drupal_group_js()
 * @see drupal_pre_render_scripts()
 */
function magic_aggregate_js(&$js_groups) {
  // Only aggregate when the site is configured to do so, and not during an
  // update.
  if (variable_get('preprocess_js', FALSE) && (!defined('MAINTENANCE_MODE') || MAINTENANCE_MODE != 'update')) {
    foreach ($js_groups as $key => $group) {
      if (isset($group['type']) && $group['type'] == 'file' && $group['preprocess']) {
        $js_groups[$key]['data'] = magic_build_js_cache($group['items']);
      }
    }
  }
}

/**
 * Aggregates JavaScript files into a cache file in the files directory.
 *
 * The file name for the JavaScript cache file is generated from the hash of
 * the aggregated contents of the files in $files. This forces proxies and
 * browsers to download new JavaScript when the JavaScript changes.
 *
 * The cache file name is retrieved on a page load via a lookup variable that
 * contains an associative array. The array key is the hash of the names in
 * $files while the value is the cache file name. The cache file is generated
 * in two cases. First, if there is no file name value for the key, which will
 * happen if a new file name has been added to $files or after the lookup
 * variable is emptied to force a rebuild of the cache. Second, the cache file
 * is generated if it is missing on disk. Old cache files are not deleted
 * immediately when the lookup variable is emptied, but are deleted after a set
 * period by drupal_delete_file_if_stale(). This ensures that files referenced
 * by a cached page will still be available.
 *
 * @param $files
 *   An array of JavaScript files to aggregate and compress into one file.
 *
 * @return
 *   The URI of the cache file, or FALSE if the file could not be saved.
 */
function magic_build_js_cache($files) {
  $contents = '';
  $uri = '';
  $map = variable_get('drupal_js_cache_files', array());
  // Create a new array so that only the file names are used to create the hash.
  // This prevents new aggregates from being created unnecessarily.
  $js_data = array();
  foreach ($files as $file) {
    $js_data[] = $file['data'];
  }
  $key = hash('sha256', serialize($js_data));
  if (isset($map[$key])) {
    $uri = $map[$key];
  }

  if (empty($uri) || !file_exists($uri)) {
    // Build aggregate JS file.
    foreach ($files as $path => $info) {
      $info['preprocess'] = isset($info['preprocess']) ? $info['preprocess'] : FALSE;
      if ($info['preprocess']) {
        // Append a ';' and a newline after each JS file to prevent them from running together.
        $contents .= file_get_contents($info['data']) . ";\n";
      }
    }
    // Prefix filename to prevent blocking by firewalls which reject files
    // starting with "ad*".
    $filename = 'js_' . drupal_hash_base64($contents) . '.js';
    // Create the js/ within the files folder.
    $jspath = 'public://js';
    $uri = $jspath . '/' . $filename;
    // Create the JS file.
    file_prepare_directory($jspath, FILE_CREATE_DIRECTORY);
    if (!file_exists($uri) && !file_unmanaged_save_data($contents, $uri, FILE_EXISTS_REPLACE)) {
      return FALSE;
    }
    // If JS gzip compression is enabled, clean URLs are enabled (which means
    // that rewrite rules are working) and the zlib extension is available then
    // create a gzipped version of this file. This file is served conditionally
    // to browsers that accept gzip using .htaccess rules.
    if (variable_get('js_gzip_compression', TRUE) && variable_get('clean_url', 0) && extension_loaded('zlib')) {
      if (!file_exists($uri . '.gz') && !file_unmanaged_save_data(gzencode($contents, 9, FORCE_GZIP), $uri . '.gz', FILE_EXISTS_REPLACE)) {
        return FALSE;
      }
    }
    $map[$key] = $uri;
    variable_set('drupal_js_cache_files', $map);
  }
  return $uri;
}

/**
 * Sorts CSS and JavaScript resources.
 *
 * Callback for uasort() within:
 * - drupal_get_css()
 * - magic_experimental_js()
 *
 * This sort order helps optimize front-end performance while providing modules
 * and themes with the necessary control for ordering the CSS and JavaScript
 * appearing on a page.
 *
 * @param $a
 *   First item for comparison. The compared items should be associative arrays
 *   of member items from drupal_add_css() or magic_add_js().
 * @param $b
 *   Second item for comparison.
 *
 * @see drupal_add_css()
 * @see magic_add_js()
 */
function magic_sort_css_js($a, $b) {
  // First order by group, so that, for example, all items in the CSS_SYSTEM
  // group appear before items in the CSS_DEFAULT group, which appear before
  // all items in the CSS_THEME group. Modules may create additional groups by
  // defining their own constants.
  if (!empty($a['group']) && !empty($b['group'])) {
    if ($a['group'] < $b['group']) {
      return -1;
    }
    elseif ($a['group'] > $b['group']) {
      return 1;
    }
  }

  // Within a group, order all infrequently needed, page-specific files after
  // common files needed throughout the website. Separating this way allows for
  // the aggregate file generated for all of the common files to be reused
  // across a site visit without being cut by a page using a less common file.
  if (!empty($a['every_page']) && !empty($b['every_page'])) {
    if ($a['every_page'] && !$b['every_page']) {
      return -1;
    }
    elseif (!$a['every_page'] && $b['every_page']) {
      return 1;
    }
  }

  // Finally, order by weight.
  if (!empty($a['weight']) && !empty($b['weight'])) {
    if ($a['weight'] < $b['weight']) {
      return -1;
    }
    elseif ($a['weight'] > $b['weight']) {
      return 1;
    }
  }

  else {
    return 0;
  }
}
