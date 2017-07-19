<h2 id="{{Class_anchor}}">
  {{#Class_has_template_parameter}}template {{/Class_has_template_parameter}}
  {{Class_kind}} {{Class_name}}
  {{#Class_superclass}}
  : public <a href="{{Type_url}}">{{Type_name}}</a>
  {{/Class_superclass}}
</h2>

{{! include header, namespace, and template parameters }}
<p><code>
  #include &lt;<a href="#">{{File_full_path}}</a>&gt;<br>
  namespace {{#Class_scope}}<a href="{{File_package_url}}">{{Type_full_name}}</a>{{/Class_scope}}
  {{#Class_has_template_parameter}}
    <br><br>
    template &lt;
    {{#Class_template_parameter}}
      {{#Class_not_first_template_parameter}}, {{/Class_not_first_template_parameter}}
      typename {{TemplateParameter_name}}
      {{#TemplateParameter_default_value}}
        = {{Expression_linked_text}}
      {{/TemplateParameter_default_value}}
    {{/Class_template_parameter}}
    &gt;
  {{/Class_has_template_parameter}}
</code></p>

{{! description }}
<p>{{Class_brief}}</p>
{{Class_details}}

{{#Class_has_subclass}}
<p>Known subclasses:</p>
<ul>
{{#Class_subclass}}
  <li><code><a href="{{Type_url}}">{{Type_name}}</a></code></li>
{{/Class_subclass}}
</ul>
{{/Class_has_subclass}}

{{! member table }}
<table>
  <tr>
    <th colspan="2">
      <h3 style="margin-top: 4px">Members</h3>
    </th>
  </tr>
  {{#Class_member}}
    {{$brief_member.tpl}}
  {{/Class_member}}
  {{#Class_section}}
    <tr>
      <th colspan="2">
        <h3 style="margin-top: 4px; margin-bottom: 4px;">{{Section_title}}</h3>
        <div style="font-style: italic; font-weight: normal;">
          {{Section_description}}
        </div>
      </th>
    </tr>
    {{#Section_member}}
      {{$brief_member.tpl}}
    {{/Section_member}}
  {{/Class_section}}
</table>

{{! member details }}
{{#Class_member}}
  {{$detailed_member.tpl}}
{{/Class_member}}
{{#Class_section}}
  {{#Section_member}}
    {{$detailed_member.tpl}}
  {{/Section_member}}
{{/Class_section}}
