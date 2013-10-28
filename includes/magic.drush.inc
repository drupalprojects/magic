<?php

/**
 * Implements hook_drush_cache_clear().
 */
function magic_drush_cache_clear(&$types) {
  $types['magic'] = 'magic_clear_cache';
}
