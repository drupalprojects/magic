<?php

/**
 * @file
 * Hooks provided by the Magic module
 */

/**
 * @addtogroup hooks
 * @{
 */

/**
 * Allows altering of the magic settings array.
 *
 * Magic has to change it's weight to be far lower than other modules in the
 * system to allow for the CSS / JS alter hooks to work correctly. As such, we
 * need to allow other modules to alter or add their own settings into the magic
 * module hook. We created this alter hook just for that purpose.
 *
 * @param array $magic_settings
 *   The renderable form array of the magic module theme settings.
 * @param string $theme
 *   The theme that the settings will be editing.
 *
 * @return
 *   The array of settings within the magic module theme page.
 */
function hook_magic_theme_settings_alter(&$magic_settings, $theme) {

  $magic_settings['css']['my_own_css_settings'] = array(
    '#type' => 'checkbox',
    '#title' => t('Do you want to?'),
  );

  // Add in your own submit handlers.
  $magic_settings['#submit'][] = 'my_module_magic_custom_submit_function';

  return $magic_settings;
}


/**
 * @} End of "addtogroup hooks".
 */
