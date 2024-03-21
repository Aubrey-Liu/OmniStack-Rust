use proc_macro::TokenStream;
use proc_macro2::Span;
use proc_macro_crate::{crate_name, FoundCrate};
use quote::quote;
use syn::{parse_macro_input, DeriveInput, Ident};

fn import_crate(name: &str) -> proc_macro2::TokenStream {
    match crate_name(name).unwrap() {
        FoundCrate::Itself => quote!(crate),
        FoundCrate::Name(name) => {
            let ident = Ident::new(&name, Span::call_site());
            quote!(::#ident)
        }
    }
}

#[proc_macro_derive(Module)]
pub fn register_module_derive(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let struct_name = &input.ident;

    let my_crate = import_crate("omnistack-core");

    let output = quote! {
        #my_crate::paste! {
            mod [<__priv_ #struct_name:lower>] {
                #![allow(non_upper_case_globals)]
                use super::#struct_name;
                #[#my_crate::constructor]
                extern "C" fn [<#struct_name:lower>]() {
                    #my_crate::module::register(stringify!(#struct_name), #struct_name::new)
                }
            }
        }
    };

    output.into()
}

#[proc_macro_derive(Adapter)]
pub fn register_adapter_derive(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let struct_name = &input.ident;

    let my_crate = import_crate("omnistack-core");

    let output = quote! {
        #my_crate::paste! {
            mod [<__priv_ #struct_name:lower>] {
                #![allow(non_upper_case_globals)]
                use super::#struct_name;
                #[#my_crate::constructor]
                extern "C" fn [<#struct_name:lower>]() {
                    #my_crate::nio::register(stringify!(#struct_name), #struct_name::new)
                }
            }
        }
    };

    output.into()
}
